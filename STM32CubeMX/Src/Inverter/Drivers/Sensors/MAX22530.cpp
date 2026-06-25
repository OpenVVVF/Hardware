#include "Inverter/Drivers/Sensors/MAX22530.h"

#include <cmath>

namespace Inverter {

namespace {

/* MAX22530 register map */
constexpr uint8_t REG_PROD_ID           = 0x00;
constexpr uint8_t REG_ADC1              = 0x01;
constexpr uint8_t REG_ADC2              = 0x02;
constexpr uint8_t REG_ADC3              = 0x03;
constexpr uint8_t REG_ADC4              = 0x04;
constexpr uint8_t REG_INTERRUPT_STATUS  = 0x12;
constexpr uint8_t REG_INTERRUPT_ENABLE  = 0x13;
constexpr uint8_t REG_CONTROL           = 0x14;

constexpr uint16_t PROD_ID_DEVICE_MASK  = 0x007FU; /* device ID in bits [6:0] */
constexpr uint16_t PROD_ID_DEVICE       = 0x0001U; /* MAX22530 */
constexpr uint16_t PROD_ID_POR_FLAG     = 0x0080U; /* cleared by CONTROL.CLRPOR */

constexpr uint16_t ADC_DATA_MASK        = 0x0FFF;
constexpr float    VREF                 = 1.80f;
constexpr float    ADC_COUNTS           = 4096.0f;

constexpr uint16_t EEOC_BIT             = (1U << 12); /* INTERRUPT_ENABLE */

constexpr uint32_t SPI_TIMEOUT_MS       = 25U;

/* One instance per EXTI line (pin number 0..15). */
MAX22530* s_instances_by_pin[16] = { nullptr };

/* For a single SPI2 peripheral there is only one driver instance that will
 * ever use DMA.  Keep a direct pointer so the HAL DMA callbacks know where to
 * dispatch without scanning the pin table. */
MAX22530* s_spi2_instance = nullptr;

/* DMA buffers must live in AXI SRAM, not DTCMRAM. */
constexpr uint8_t BURST_LEN = 11U;
static uint8_t s_dma_tx[BURST_LEN] __attribute__((section(".dma_buffers")));
static uint8_t s_dma_rx[BURST_LEN] __attribute__((section(".dma_buffers")));

int pinIndex(uint16_t pin) {
    if (pin == 0) return -1;
    return __builtin_ctz(pin);
}

} // namespace

MAX22530::MAX22530(SPI_HandleTypeDef* hspi,
                   GPIO_TypeDef* cs_port, uint16_t cs_pin,
                   GPIO_TypeDef* int_port, uint16_t int_pin,
                   IRQn_Type int_irqn)
    : m_hspi(hspi),
      m_cs_port(cs_port),
      m_cs_pin(cs_pin),
      m_int_port(int_port),
      m_int_pin(int_pin),
      m_int_irqn(int_irqn) {
    const int idx = pinIndex(m_int_pin);
    if (idx >= 0 && idx < 16) {
        s_instances_by_pin[idx] = this;
    }

    /* Pre-load the DMA TX buffer with the ADC1 burst-read command.
     * The rest of the bytes are don't-care MOSI clocks. */
    s_dma_tx[0] = static_cast<uint8_t>((REG_ADC1 << 2) | 1U);
    for (int i = 1; i < BURST_LEN; ++i) {
        s_dma_tx[i] = 0x00;
    }
}

bool MAX22530::init() {
    if (!m_hspi || !m_cs_port || !m_int_port) {
        return false;
    }

    /* The SPI handle is only fully initialized after MX_SPI2_Init() runs, which
     * happens after C++ static constructors.  Register the DMA callback target
     * here, once the peripheral instance field is valid. */
    if (m_hspi->Instance == SPI2) {
        s_spi2_instance = this;
    }

    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);

    /* Allow the isolated field-side DC-DC and ADC to finish power-up. */
    HAL_Delay(50);

    /* Verify device ID.  The lower 7 bits are the fixed MAX22530 device ID
     * (0x01); bit 7 is the POR flag that is set after a power or hard reset
     * and is cleared by CONTROL.CLRPOR.  Accepting 0x01 or 0x81 keeps init
     * from failing across debugger resets where the part retained power. */
    uint16_t prod_id = 0;
    if (!readRegister(REG_PROD_ID, prod_id) ||
        (prod_id & PROD_ID_DEVICE_MASK) != PROD_ID_DEVICE) {
        return false;
    }

    /* If the POR flag is already clear the device retained state from a
     * previous boot (debugger reset without power cycle).  Force a hard reset
     * so the field-side DC-DC restarts from a known-good state. */
    if ((prod_id & PROD_ID_POR_FLAG) == 0) {
        if (!writeRegister(REG_CONTROL, 0x0001U)) { /* REST */
            return false;
        }
        HAL_Delay(100);

        if (!readRegister(REG_PROD_ID, prod_id) ||
            (prod_id & PROD_ID_DEVICE_MASK) != PROD_ID_DEVICE) {
            return false;
        }
    }

    /* Clear the power-on-reset flag. */
    if (!writeRegister(REG_CONTROL, 0x0004U)) { /* CLRPOR */
        return false;
    }
    HAL_Delay(5);

    /* Make sure field-side power is enabled and CRC stays disabled. */
    if (!writeRegister(REG_CONTROL, 0x0000U)) {
        return false;
    }

    /* Enable end-of-conversion hardware interrupt.  Leave all other
     * interrupt sources disabled so the INT pin only toggles on new data. */
    if (!writeRegister(REG_INTERRUPT_ENABLE, EEOC_BIT)) {
        return false;
    }

    /* Clear any stale status so the INT line starts high. */
    uint16_t int_status = 0;
    (void)readRegister(REG_INTERRUPT_STATUS, int_status);

    /* Configure the INT pin as a falling-edge EXTI input.  INT is
     * open-drain active-low, so enable the internal pull-up. */
    GPIO_InitTypeDef gpio = {};
    gpio.Pin = m_int_pin;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(m_int_port, &gpio);

    /* Lower numeric priority = higher urgency.  PWM and current-sense ISRs
     * run at 4-5; keep this one clearly in the background. */
    HAL_NVIC_SetPriority(m_int_irqn, 14, 0);
    HAL_NVIC_EnableIRQ(m_int_irqn);

    return true;
}

void MAX22530::onInterrupt() {
    /* The INT pin is open-drain active-low and stays low until the interrupt
     * status register is read.  Start a SPI DMA burst read so the line is
     * released as soon as the transfer finishes; the CPU is not tied up during
     * the ~15 us transfer. */
    ++m_irq_cnt;

    if (m_dma_busy) {
        return;
    }

    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_RESET);
    m_dma_busy = true;

    if (HAL_SPI_TransmitReceive_DMA(m_hspi, s_dma_tx, s_dma_rx, BURST_LEN) != HAL_OK) {
        ++m_dma_start_fail_cnt;
        HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);
        m_dma_busy = false;
    }
}

void MAX22530::onDmaComplete() {
    ++m_dma_cnt;
    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);
    m_dma_busy = false;

    /* Burst read layout: byte 0 = MISO dummy, bytes 1-2 = ADC1, 3-4 = ADC2,
     * 5-6 = ADC3, 7-8 = ADC4, 9-10 = INTERRUPT_STATUS. */
    for (int i = 0; i < 4; ++i) {
        const uint16_t raw = static_cast<uint16_t>((s_dma_rx[2 * i + 1] << 8) |
                                                    s_dma_rx[2 * i + 2]);
        m_voltages[i] = (static_cast<float>(raw & ADC_DATA_MASK) * VREF) / ADC_COUNTS;
    }

    m_data_ready = true;
}

void MAX22530::onDmaError() {
    ++m_err_cnt;
    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);
    m_dma_busy = false;
}

MAX22530* MAX22530::instanceForPin(uint16_t pin) {
    const int idx = pinIndex(pin);
    if (idx < 0 || idx >= 16) {
        return nullptr;
    }
    return s_instances_by_pin[idx];
}

bool MAX22530::readRegister(uint8_t reg, uint16_t& out) {
    const uint8_t cmd = static_cast<uint8_t>(reg << 2);
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = {};

    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_RESET);
    const HAL_StatusTypeDef status =
        HAL_SPI_TransmitReceive(m_hspi, tx, rx, 3, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);

    if (status != HAL_OK) {
        return false;
    }
    out = static_cast<uint16_t>((rx[1] << 8) | rx[2]);
    return true;
}

bool MAX22530::writeRegister(uint8_t reg, uint16_t value) {
    const uint8_t cmd = static_cast<uint8_t>((reg << 2) | (1U << 1));
    uint8_t tx[3] = { cmd,
                      static_cast<uint8_t>((value >> 8) & 0xFF),
                      static_cast<uint8_t>(value & 0xFF) };

    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_RESET);
    const HAL_StatusTypeDef status =
        HAL_SPI_Transmit(m_hspi, tx, 3, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);

    return (status == HAL_OK);
}


void MAX22530::update() {
    /* The SPI read already happened in onInterrupt(); just acknowledge the
     * new-sample flag for callers that use dataReady(). */
    m_data_ready = false;
}

/* -------------------------------------------------------------------------- */
/* EXTI dispatch                                                              */
/* -------------------------------------------------------------------------- */

extern "C" void EXTI1_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    /* GPIO_Pin is a bit mask (e.g. GPIO_PIN_1 = 0x0002), not an index. */
    const int idx = pinIndex(GPIO_Pin);
    if (idx >= 0 && idx < 16 && s_instances_by_pin[idx]) {
        s_instances_by_pin[idx]->onInterrupt();
    }
}

extern "C" void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef* hspi) {
    if (hspi != nullptr && hspi->Instance == SPI2 && s_spi2_instance != nullptr) {
        s_spi2_instance->onDmaComplete();
    }
}

extern "C" void HAL_SPI_ErrorCallback(SPI_HandleTypeDef* hspi) {
    if (hspi != nullptr && hspi->Instance == SPI2 && s_spi2_instance != nullptr) {
        s_spi2_instance->onDmaError();
    }
}

} // namespace Inverter
