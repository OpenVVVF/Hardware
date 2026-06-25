#include "Inverter/Drivers/Sensors/MAX22530.h"
#include "Inverter/Telemetry.h"

#include <cstdio>
#include <cstring>
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

int pinIndex(uint16_t pin) {
    if (pin == 0) return -1;
    return __builtin_ctz(pin);
}

} // namespace

MAX22530::MAX22530(SPI_HandleTypeDef* hspi,
                   GPIO_TypeDef* cs_port, uint16_t cs_pin,
                   GPIO_TypeDef* int_port, uint16_t int_pin,
                   IRQn_Type int_irqn,
                   const char* name_prefix)
    : m_hspi(hspi),
      m_cs_port(cs_port),
      m_cs_pin(cs_pin),
      m_int_port(int_port),
      m_int_pin(int_pin),
      m_int_irqn(int_irqn) {
    std::memset(m_prefix, 0, sizeof(m_prefix));
    if (name_prefix) {
        std::strncpy(m_prefix, name_prefix, sizeof(m_prefix) - 1);
    }

    const int idx = pinIndex(m_int_pin);
    if (idx >= 0 && idx < 16) {
        s_instances_by_pin[idx] = this;
    }
}

bool MAX22530::init() {
    if (!m_hspi || !m_cs_port || !m_int_port) {
        Telemetry::log("print", "[MAX22530] init failed: null handle");
        return false;
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
        char msg[64];
        std::snprintf(msg, sizeof(msg),
                      "[MAX22530] init failed: bad PROD_ID=0x%04X",
                      prod_id);
        Telemetry::log("print", msg);
        return false;
    }

    /* If the POR flag is already clear the device retained state from a
     * previous boot (debugger reset without power cycle).  Force a hard reset
     * so the field-side DC-DC restarts from a known-good state. */
    if ((prod_id & PROD_ID_POR_FLAG) == 0) {
        Telemetry::log("print", "[MAX22530] POR clear, issuing hard reset");
        if (!writeRegister(REG_CONTROL, 0x0001U)) { /* REST */
            Telemetry::log("print", "[MAX22530] hard reset write failed");
            return false;
        }
        HAL_Delay(100);

        if (!readRegister(REG_PROD_ID, prod_id) ||
            (prod_id & PROD_ID_DEVICE_MASK) != PROD_ID_DEVICE) {
            char msg[64];
            std::snprintf(msg, sizeof(msg),
                          "[MAX22530] no response after hard reset: PROD_ID=0x%04X",
                          prod_id);
            Telemetry::log("print", msg);
            return false;
        }
    }

    /* Clear the power-on-reset flag. */
    if (!writeRegister(REG_CONTROL, 0x0004U)) { /* CLRPOR */
        Telemetry::log("print", "[MAX22530] failed to clear POR");
        return false;
    }
    HAL_Delay(5);

    /* Make sure field-side power is enabled and CRC stays disabled. */
    if (!writeRegister(REG_CONTROL, 0x0000U)) {
        Telemetry::log("print", "[MAX22530] failed to write CONTROL");
        return false;
    }

    uint16_t control = 0;
    (void)readRegister(REG_CONTROL, control);

    /* Enable end-of-conversion hardware interrupt.  Leave all other
     * interrupt sources disabled so the INT pin only toggles on new data. */
    if (!writeRegister(REG_INTERRUPT_ENABLE, EEOC_BIT)) {
        Telemetry::log("print", "[MAX22530] failed to enable EOC interrupt");
        return false;
    }

    /* Verify the write and clear any stale status. */
    uint16_t int_enable = 0;
    uint16_t int_status = 0;
    (void)readRegister(REG_INTERRUPT_ENABLE, int_enable);
    (void)readRegister(REG_INTERRUPT_STATUS, int_status);
    {
        char dbg[48];
        std::snprintf(dbg, sizeof(dbg),
                      "[MAX22530] CTRL=0x%04X EN=0x%04X",
                      control, int_enable);
        Telemetry::log("print", dbg);
        std::snprintf(dbg, sizeof(dbg),
                      "[MAX22530] STATUS=0x%04X",
                      int_status);
        Telemetry::log("print", dbg);
    }

    /* Configure the INT pin as a falling-edge EXTI input.  INT is
     * open-drain active-low, so enable the internal pull-up. */
    GPIO_InitTypeDef gpio = {};
    gpio.Pin = m_int_pin;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(m_int_port, &gpio);

    /* Lower numeric priority = higher urgency.  Motor/encoder run at 5,
     * so use 12 to keep this ISR clearly in the background. */
    HAL_NVIC_SetPriority(m_int_irqn, 12, 0);
    HAL_NVIC_EnableIRQ(m_int_irqn);

    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "[MAX22530] init ok ID=0x%04X, EOC int enabled",
                  prod_id);
    Telemetry::log("print", msg);

    /* Telemetry indicator that the chip was detected and configured. */
    {
        char ok_key[32];
        std::snprintf(ok_key, sizeof(ok_key), "%s_init_ok", m_prefix);
        Telemetry::log(ok_key, 1.0f);
    }
    return true;
}

void MAX22530::onInterrupt() {
    m_data_ready = true;
    ++m_interrupt_count;
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

bool MAX22530::burstReadAdc(uint16_t raw[4], uint16_t& status) {
    /* Burst read starts at ADC1 and returns ADC1, ADC2, ADC3, ADC4, and
     * INTERRUPT_STATUS in one transaction. */
    const uint8_t cmd = static_cast<uint8_t>((REG_ADC1 << 2) | 1U);
    uint8_t tx[11] = { cmd, 0 };
    uint8_t rx[11] = {};

    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_RESET);
    const HAL_StatusTypeDef hal_status =
        HAL_SPI_TransmitReceive(m_hspi, tx, rx, 11, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);

    if (hal_status != HAL_OK) {
        return false;
    }

    raw[0] = static_cast<uint16_t>((rx[1] << 8) | rx[2]);
    raw[1] = static_cast<uint16_t>((rx[3] << 8) | rx[4]);
    raw[2] = static_cast<uint16_t>((rx[5] << 8) | rx[6]);
    raw[3] = static_cast<uint16_t>((rx[7] << 8) | rx[8]);
    status = static_cast<uint16_t>((rx[9] << 8) | rx[10]);
    return true;
}

void MAX22530::publish() {
    char key[32];
    for (int i = 0; i < 4; ++i) {
        std::snprintf(key, sizeof(key), "%s_adc%d_v", m_prefix, i + 1);
        Telemetry::log(key, m_voltages[i]);
    }

    std::snprintf(key, sizeof(key), "%s_status", m_prefix);
    Telemetry::log(key, static_cast<float>(m_int_status));
}

void MAX22530::update() {
    const uint32_t now_ms = HAL_GetTick();

    /* Always publish interrupt diagnostic telemetry so we can see whether the
     * EXTI line is firing and what the INT pin is doing, even when no SPI read
     * happens this cycle. */
    {
        char key[32];
        std::snprintf(key, sizeof(key), "%s_int_cnt", m_prefix);
        Telemetry::log(key, static_cast<float>(m_interrupt_count));

        const float pin_state =
            (HAL_GPIO_ReadPin(m_int_port, m_int_pin) == GPIO_PIN_SET) ? 1.0f : 0.0f;
        std::snprintf(key, sizeof(key), "%s_int_pin", m_prefix);
        Telemetry::log(key, pin_state);
    }

    if (!m_data_ready) {
        if (!m_warned_no_int && m_interrupt_count == 0 &&
            (now_ms - m_last_data_ms) > 1000U) {
            Telemetry::log("print", "[MAX22530] no EOC interrupts received");
            m_warned_no_int = true;
        }
        return;
    }
    m_data_ready = false;
    m_last_data_ms = now_ms;
    m_warned_no_int = false;

    uint16_t raw[4] = {};
    if (!burstReadAdc(raw, m_int_status)) {
        Telemetry::log("print", "[MAX22530] burst read failed");
        return;
    }

    for (int i = 0; i < 4; ++i) {
        m_voltages[i] = (static_cast<float>(raw[i] & ADC_DATA_MASK) * VREF) / ADC_COUNTS;
    }

    if (m_first_data) {
        Telemetry::log("print", "[MAX22530] first ADC data received");
        m_first_data = false;
    }

    publish();
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

} // namespace Inverter
