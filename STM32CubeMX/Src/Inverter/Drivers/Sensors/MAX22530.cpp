#include "Inverter/Drivers/Sensors/MAX22530.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Telemetry.h"

#include <cmath>
#include <cstring>

namespace Inverter {

namespace {

/* MAX22530 register map */
constexpr uint8_t REG_PROD_ID           = 0x00;
constexpr uint8_t REG_ADC1              = 0x01;
constexpr uint8_t REG_ADC2              = 0x02;
constexpr uint8_t REG_ADC3              = 0x03;
constexpr uint8_t REG_ADC4              = 0x04;
constexpr uint8_t REG_FADC1             = 0x05;
constexpr uint8_t REG_FADC2             = 0x06;
constexpr uint8_t REG_FADC3             = 0x07;
constexpr uint8_t REG_FADC4             = 0x08;
constexpr uint8_t REG_COUTHI1           = 0x09;
constexpr uint8_t REG_COUTHI2           = 0x0A;
constexpr uint8_t REG_COUTHI3           = 0x0B;
constexpr uint8_t REG_COUTHI4           = 0x0C;
constexpr uint8_t REG_COUTLO1           = 0x0D;
constexpr uint8_t REG_COUTLO2           = 0x0E;
constexpr uint8_t REG_COUTLO3           = 0x0F;
constexpr uint8_t REG_COUTLO4           = 0x10;
constexpr uint8_t REG_COUT_STATUS       = 0x11;
constexpr uint8_t REG_INTERRUPT_STATUS  = 0x12;
constexpr uint8_t REG_INTERRUPT_ENABLE  = 0x13;
constexpr uint8_t REG_CONTROL           = 0x14;

/* PROD_ID decode */
constexpr uint16_t PROD_ID_DEVICE_MASK  = 0x007FU; /* device ID in bits [6:0] */
constexpr uint16_t PROD_ID_DEVICE       = 0x0001U; /* MAX22530 */
constexpr uint16_t PROD_ID_POR_FLAG     = 0x0080U; /* cleared by CONTROL.CLRPOR */

/* CONTROL register bits */
constexpr uint16_t CONTROL_REST         = (1U << 0);
constexpr uint16_t CONTROL_SRES         = (1U << 1);
constexpr uint16_t CONTROL_CLRPOR       = (1U << 2);
constexpr uint16_t CONTROL_DISPWR       = (1U << 3);
constexpr uint16_t CONTROL_FLT_CLR_1    = (1U << 4);
constexpr uint16_t CONTROL_FLT_CLR_2    = (1U << 5);
constexpr uint16_t CONTROL_FLT_CLR_3    = (1U << 6);
constexpr uint16_t CONTROL_FLT_CLR_4    = (1U << 7);
constexpr uint16_t CONTROL_ECOM         = (1U << 14);
constexpr uint16_t CONTROL_ENCRC        = (1U << 15);

/* INTERRUPT_ENABLE / INTERRUPT_STATUS bits */
constexpr uint16_t INT_CO_NEG_1         = (1U << 0);
constexpr uint16_t INT_CO_NEG_2         = (1U << 1);
constexpr uint16_t INT_CO_NEG_3         = (1U << 2);
constexpr uint16_t INT_CO_NEG_4         = (1U << 3);
constexpr uint16_t INT_CO_POS_1         = (1U << 4);
constexpr uint16_t INT_CO_POS_2         = (1U << 5);
constexpr uint16_t INT_CO_POS_3         = (1U << 6);
constexpr uint16_t INT_CO_POS_4         = (1U << 7);
constexpr uint16_t INT_SPICRC           = (1U << 8);
constexpr uint16_t INT_SPIFRM           = (1U << 9);
constexpr uint16_t INT_FLD              = (1U << 10);
constexpr uint16_t INT_ADCF             = (1U << 11);
constexpr uint16_t INT_EEOC             = (1U << 12);

/* COUTHI register bits */
constexpr uint16_t COUTHI_CO_IN_SEL     = (1U << 14); /* 1 = filtered, 0 = raw */
constexpr uint16_t COUTHI_CO_MODE       = (1U << 15); /* 1 = digital status, 0 = digital input */
constexpr uint16_t COUTHI_THRESHOLD_MASK = 0x0FFFU;

constexpr uint16_t ADC_DATA_MASK        = 0x0FFFU;
constexpr float    VREF                 = 1.80f;
constexpr float    ADC_COUNTS           = 4096.0f;

constexpr uint32_t SPI_TIMEOUT_MS       = 25U;

/* One instance per EXTI line (pin number 0..15). */
MAX22530* s_instances_by_pin[16] = { nullptr };

/* For a single SPI2 peripheral there is only one driver instance that will
 * ever use DMA.  Keep a direct pointer so the HAL DMA callbacks know where to
 * dispatch without scanning the pin table. */
MAX22530* s_spi2_instance = nullptr;

/* DMA buffers must live in AXI SRAM, not DTCMRAM.  Size is the maximum burst
 * length: 11 bytes without CRC, 12 bytes with CRC. */
static uint8_t s_dma_tx[MAX22530::MAX_BURST_LEN] __attribute__((section(".dma_buffers")));
static uint8_t s_dma_rx[MAX22530::MAX_BURST_LEN] __attribute__((section(".dma_buffers")));

int pinIndex(uint16_t pin) {
    if (pin == 0) return -1;
    return __builtin_ctz(pin);
}

constexpr uint16_t controlFilterClearBit(uint8_t channel) {
    switch (channel) {
        case 0: return CONTROL_FLT_CLR_1;
        case 1: return CONTROL_FLT_CLR_2;
        case 2: return CONTROL_FLT_CLR_3;
        case 3: return CONTROL_FLT_CLR_4;
        default: return 0;
    }
}

constexpr uint8_t channelPosBit(uint8_t channel) {
    switch (channel) {
        case 0: return 4;
        case 1: return 5;
        case 2: return 6;
        case 3: return 7;
        default: return 0;
    }
}

constexpr uint8_t channelNegBit(uint8_t channel) {
    switch (channel) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        default: return 0;
    }
}

/**
 * @brief CRC-8 used by the MAX22530: x^8 + x^2 + x + 1 (poly = 0x07).
 */
uint8_t crc8Max22530(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; ++b) {
            if (crc & 0x80) {
                crc = static_cast<uint8_t>((crc << 1) ^ 0x07);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
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

    /* Pre-load the DMA TX buffer with the FADC1 burst-read command to get the
     * filtered (rolling average) ADC values.  The rest of the bytes are
     * don't-care MOSI clocks. */
    updateDmaTxBuffer();
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
        if (!reset()) {
            return false;
        }
    }

    /* Clear the power-on-reset flag. */
    if (!clearPOR()) {
        return false;
    }
    HAL_Delay(5);

    /* Make sure field-side power is enabled and CRC stays disabled. */
    if (!writeRegister(REG_CONTROL, 0x0000U)) {
        return false;
    }

    /* Enable end-of-conversion hardware interrupt.  Comparator interrupts are
     * enabled later when thresholds are configured. */
    if (!writeRegister(REG_INTERRUPT_ENABLE, INT_EEOC)) {
        return false;
    }
    m_int_enable = INT_EEOC;

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

bool MAX22530::reset() {
    /* Hard reset (CONTROL.REST): restarts field-side power and logic core. */
    if (!resetInternal(CONTROL_REST)) {
        return false;
    }
    HAL_Delay(100);

    uint16_t prod_id = 0;
    if (!readRegister(REG_PROD_ID, prod_id) ||
        (prod_id & PROD_ID_DEVICE_MASK) != PROD_ID_DEVICE) {
        return false;
    }

    return true;
}

bool MAX22530::softReset() {
    /* Soft reset (CONTROL.SRES): resets logic core registers only. */
    if (!resetInternal(CONTROL_SRES)) {
        return false;
    }
    HAL_Delay(10);

    uint16_t prod_id = 0;
    if (!readRegister(REG_PROD_ID, prod_id) ||
        (prod_id & PROD_ID_DEVICE_MASK) != PROD_ID_DEVICE) {
        return false;
    }

    return true;
}

bool MAX22530::resetInternal(uint16_t control_bits) {
    if (!writeRegister(REG_CONTROL, control_bits)) {
        return false;
    }
    return true;
}

bool MAX22530::clearPOR() {
    return writeRegister(REG_CONTROL, CONTROL_CLRPOR);
}

bool MAX22530::clearFilter(uint8_t channel) {
    if (channel > 3) {
        return false;
    }

    const uint16_t bit = controlFilterClearBit(channel);
    if (!writeRegister(REG_CONTROL, bit)) {
        return false;
    }

    /* The chip auto-clears the filter-clear bits after acting on them.  Write
     * a normal control word to return to the running state. */
    HAL_Delay(1);
    return writeRegister(REG_CONTROL, m_crc_enabled ? CONTROL_ENCRC : 0x0000U);
}

bool MAX22530::enableCRC(bool enable) {
    uint16_t control = 0;
    if (enable) {
        control = CONTROL_ENCRC;
    }

    if (!writeRegister(REG_CONTROL, control)) {
        return false;
    }

    m_crc_enabled = enable;
    updateDmaTxBuffer();
    return true;
}

bool MAX22530::readRegister(uint8_t reg, uint16_t& out) {
    const uint8_t cmd = static_cast<uint8_t>(reg << 2);

    if (!m_crc_enabled) {
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
    } else {
        uint8_t tx[4] = { cmd, 0x00, 0x00, 0x00 };
        tx[3] = crc8Max22530(tx, 3);
        uint8_t rx[4] = {};

        HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_RESET);
        const HAL_StatusTypeDef status =
            HAL_SPI_TransmitReceive(m_hspi, tx, rx, 4, SPI_TIMEOUT_MS);
        HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);

        if (status != HAL_OK) {
            return false;
        }

        const uint8_t expected = crc8Max22530(rx, 3);
        if (expected != rx[3]) {
            ++m_crc_error_cnt;
            return false;
        }

        out = static_cast<uint16_t>((rx[1] << 8) | rx[2]);
        return true;
    }
}

bool MAX22530::writeRegister(uint8_t reg, uint16_t value) {
    const uint8_t cmd = static_cast<uint8_t>((reg << 2) | (1U << 1));

    if (!m_crc_enabled) {
        uint8_t tx[3] = { cmd,
                          static_cast<uint8_t>((value >> 8) & 0xFF),
                          static_cast<uint8_t>(value & 0xFF) };

        HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_RESET);
        const HAL_StatusTypeDef status =
            HAL_SPI_Transmit(m_hspi, tx, 3, SPI_TIMEOUT_MS);
        HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);

        return (status == HAL_OK);
    } else {
        uint8_t tx[4] = { cmd,
                          static_cast<uint8_t>((value >> 8) & 0xFF),
                          static_cast<uint8_t>(value & 0xFF),
                          0x00 };
        tx[3] = crc8Max22530(tx, 3);

        HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_RESET);
        const HAL_StatusTypeDef status =
            HAL_SPI_Transmit(m_hspi, tx, 4, SPI_TIMEOUT_MS);
        HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);

        return (status == HAL_OK);
    }
}

uint16_t MAX22530::readRawCounts(uint8_t channel) {
    if (channel > 3) {
        return 0;
    }
    uint16_t raw = 0;
    if (!readRegister(REG_ADC1 + channel, raw)) {
        return 0;
    }
    return raw & ADC_DATA_MASK;
}

uint16_t MAX22530::readFilteredCounts(uint8_t channel) {
    if (channel > 3) {
        return 0;
    }
    uint16_t raw = 0;
    if (!readRegister(REG_FADC1 + channel, raw)) {
        return 0;
    }
    return raw & ADC_DATA_MASK;
}

float MAX22530::readRawVoltage(uint8_t channel) {
    return countsToVoltage(readRawCounts(channel));
}

float MAX22530::readFilteredVoltage(uint8_t channel) {
    return countsToVoltage(readFilteredCounts(channel));
}

bool MAX22530::burstReadRaw(uint16_t out_counts[4], uint16_t* int_status) {
    return burstTransaction(REG_ADC1, out_counts, int_status);
}

bool MAX22530::burstReadFiltered(uint16_t out_counts[4], uint16_t* int_status) {
    return burstTransaction(REG_FADC1, out_counts, int_status);
}

bool MAX22530::burstTransaction(uint8_t start_reg,
                                uint16_t out_counts[4],
                                uint16_t* int_status) {
    if (out_counts == nullptr) {
        return false;
    }

    const uint8_t cmd = static_cast<uint8_t>((start_reg << 2) | 1U);
    const uint8_t len = m_crc_enabled ? 12U : 11U;
    uint8_t tx[MAX_BURST_LEN] = {};
    uint8_t rx[MAX_BURST_LEN] = {};

    tx[0] = cmd;
    /* tx[1..8] are don't-care clocks for the four 16-bit data words. */
    /* tx[9..10] are don't-care clocks for INTERRUPT_STATUS. */
    if (m_crc_enabled) {
        tx[11] = crc8Max22530(tx, 11);
    }

    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_RESET);
    const HAL_StatusTypeDef status =
        HAL_SPI_TransmitReceive(m_hspi, tx, rx, len, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);

    if (status != HAL_OK) {
        return false;
    }

    if (m_crc_enabled) {
        const uint8_t expected = crc8Max22530(rx, len - 1);
        if (expected != rx[len - 1]) {
            ++m_crc_error_cnt;
            return false;
        }
    }

    for (int i = 0; i < 4; ++i) {
        const uint16_t raw = static_cast<uint16_t>((rx[2 * i + 1] << 8) |
                                                    rx[2 * i + 2]);
        out_counts[i] = raw & ADC_DATA_MASK;
    }

    if (int_status != nullptr) {
        *int_status = static_cast<uint16_t>((rx[9] << 8) | rx[10]);
    }

    return true;
}

bool MAX22530::setComparatorThreshold(uint8_t channel, float high_v, float low_v,
                                      bool use_filtered, bool digital_status,
                                      bool enable_pos_interrupt,
                                      bool enable_neg_interrupt) {
    if (channel > 3 || high_v < low_v) {
        return false;
    }

    const uint16_t high_counts = voltageToCounts(high_v);
    const uint16_t low_counts  = voltageToCounts(low_v);

    uint16_t couthi = high_counts & COUTHI_THRESHOLD_MASK;
    if (use_filtered) {
        couthi |= COUTHI_CO_IN_SEL;
    }
    if (digital_status) {
        couthi |= COUTHI_CO_MODE;
    }

    const uint16_t coutlo = low_counts & COUTHI_THRESHOLD_MASK;

    if (!writeRegister(REG_COUTHI1 + channel, couthi)) {
        return false;
    }
    if (!writeRegister(REG_COUTLO1 + channel, coutlo)) {
        return false;
    }

    /* Update the comparator interrupts for this channel.  Clear the old bits
     * first so a later call can disable a direction (e.g. UV disabled). */
    uint16_t int_en = 0;
    if (!readRegister(REG_INTERRUPT_ENABLE, int_en)) {
        return false;
    }
    const uint16_t pos_bit = static_cast<uint16_t>(1U << channelPosBit(channel));
    const uint16_t neg_bit = static_cast<uint16_t>(1U << channelNegBit(channel));
    int_en &= ~pos_bit;
    int_en &= ~neg_bit;
    int_en |= INT_EEOC;
    if (enable_pos_interrupt) {
        int_en |= pos_bit;
    }
    if (enable_neg_interrupt) {
        int_en |= neg_bit;
    }

    if (!writeRegister(REG_INTERRUPT_ENABLE, int_en)) {
        return false;
    }
    m_int_enable = int_en;

    /* Clear any comparator events that occurred before the interrupt was
     * enabled so they are not mistaken for a new fault. */
    (void)clearInterruptStatus();
    return true;
}

bool MAX22530::getComparatorStatus(uint16_t& status) {
    return readRegister(REG_COUT_STATUS, status);
}

bool MAX22530::readComparatorThreshold(uint8_t channel,
                                       uint16_t& high_counts,
                                       uint16_t& low_counts) {
    if (channel > 3) {
        return false;
    }
    if (!readRegister(REG_COUTHI1 + channel, high_counts)) {
        return false;
    }
    if (!readRegister(REG_COUTLO1 + channel, low_counts)) {
        return false;
    }
    high_counts &= COUTHI_THRESHOLD_MASK;
    low_counts  &= COUTHI_THRESHOLD_MASK;
    return true;
}

bool MAX22530::clearInterruptStatus() {
    uint16_t dummy = 0;
    return readRegister(REG_INTERRUPT_STATUS, dummy);
}

uint16_t MAX22530::voltageToCounts(float v) const {
    if (v <= 0.0f) {
        return 0;
    }
    if (v >= VREF) {
        return ADC_DATA_MASK;
    }
    return static_cast<uint16_t>((v * ADC_COUNTS) / VREF);
}

float MAX22530::countsToVoltage(uint16_t counts) const {
    counts &= ADC_DATA_MASK;
    return (static_cast<float>(counts) * VREF) / ADC_COUNTS;
}

void MAX22530::updateDmaTxBuffer() {
    s_dma_tx[0] = static_cast<uint8_t>((REG_FADC1 << 2) | 1U);
    for (uint8_t i = 1; i < 11; ++i) {
        s_dma_tx[i] = 0x00;
    }

    if (m_crc_enabled) {
        m_burst_len = 12;
        s_dma_tx[11] = crc8Max22530(s_dma_tx, 11);
    } else {
        m_burst_len = 11;
        s_dma_tx[11] = 0x00;
    }
}

void MAX22530::onInterrupt() {
    ++m_irq_cnt;

    if (m_dma_busy) {
        return;
    }

    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_RESET);
    m_dma_busy = true;

    if (HAL_SPI_TransmitReceive_DMA(m_hspi, s_dma_tx, s_dma_rx, m_burst_len) != HAL_OK) {
        ++m_dma_start_fail_cnt;
        HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);
        m_dma_busy = false;
    }
}

void MAX22530::onDmaComplete() {
    ++m_dma_cnt;
    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);
    m_dma_busy = false;

    if (parseBurst(s_dma_rx, m_burst_len)) {
        m_data_ready = true;
    }
}

bool MAX22530::parseBurst(const uint8_t rx[MAX_BURST_LEN], uint8_t len) {
    if (m_crc_enabled && len >= 12) {
        const uint8_t expected = crc8Max22530(rx, len - 1);
        if (expected != rx[len - 1]) {
            ++m_crc_error_cnt;
            if (++m_comm_err_seq >= COMM_ERR_THRESHOLD) {
                FaultManager::instance().raise(FaultSource::Max22530Comm,
                                               FaultReason::Max22530CrcMismatch);
            }
            return false; /* Do not trust a corrupted frame. */
        }
    }

    /* Burst read layout: byte 0 = MISO dummy, bytes 1-2 = ADC/FADC1,
     * 3-4 = ADC/FADC2, 5-6 = ADC/FADC3, 7-8 = ADC/FADC4,
     * 9-10 = INTERRUPT_STATUS, [11] = CRC if enabled. */
    for (int i = 0; i < 4; ++i) {
        const uint16_t raw = static_cast<uint16_t>((rx[2 * i + 1] << 8) |
                                                    rx[2 * i + 2]);
        m_voltages[i] = countsToVoltage(raw);
    }

    m_int_status = static_cast<uint16_t>((rx[9] << 8) | rx[10]);
    raiseFaultsFromInterruptStatus(m_int_status);

    return true;
}

void MAX22530::raiseFaultsFromInterruptStatus(uint16_t status) {
    if (status == 0) {
        return;
    }

    const bool spi_fault = (status & (INT_SPIFRM | INT_SPICRC)) != 0;
    if (spi_fault) {
        if (++m_comm_err_seq >= COMM_ERR_THRESHOLD) {
            FaultManager::instance().raise(FaultSource::Max22530Comm,
                                           FaultReason::Max22530SpiFrameError);
        }
    } else {
        m_comm_err_seq = 0;
    }

    if (status & INT_ADCF) {
        FaultManager::instance().raise(FaultSource::Max22530Adc,
                                       FaultReason::Max22530AdcDiagnostic);
    }
    if (status & INT_FLD) {
        FaultManager::instance().raise(FaultSource::Max22530Field,
                                       FaultReason::Max22530FieldLoss);
    }

    /* Channel 1 (index 0) is used for DC-link Vbus in this design.
     * Ignore comparator bits in a frame that itself had an SPI error; the
     * status word may be corrupted.  Also mask by the interrupt-enable mask so
     * a disabled comparator direction cannot raise a latched fault. */
    if (!spi_fault) {
        const uint16_t comp_status = status & m_int_enable;
        if (comp_status & INT_CO_POS_1) {
            FaultManager::instance().raise(FaultSource::Max22530Ov,
                                           FaultReason::Max22530Overvoltage);
        }
        if (comp_status & INT_CO_NEG_1) {
            FaultManager::instance().raise(FaultSource::Max22530Uv,
                                           FaultReason::Max22530Undervoltage);
        }
    }
}

void MAX22530::onDmaError() {
    ++m_err_cnt;
    HAL_GPIO_WritePin(m_cs_port, m_cs_pin, GPIO_PIN_SET);
    m_dma_busy = false;
    if (++m_comm_err_seq >= COMM_ERR_THRESHOLD) {
        FaultManager::instance().raise(FaultSource::Max22530Comm,
                                       FaultReason::Max22530SpiDmaError);
    }
}

MAX22530* MAX22530::instanceForPin(uint16_t pin) {
    const int idx = pinIndex(pin);
    if (idx < 0 || idx >= 16) {
        return nullptr;
    }
    return s_instances_by_pin[idx];
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
