#pragma once

#include <cstdint>

#include "main.h"
#include "spi.h"

namespace Inverter {

/**
 * @brief Full-featured driver for the MAX22530/MAX22531/MAX22532 4-channel
 * isolated ADC.
 *
 * The default run-time mode is a filtered (FADC) DMA burst read triggered by
 * the end-of-conversion interrupt, but the driver also exposes single-read and
 * raw-read APIs for diagnostics and shell commands.
 *
 * The driver is pin-agnostic: pass the SPI handle, chip-select GPIO, and
 * hardware-interrupt GPIO in the constructor.  Only one instance on SPI2 can
 * use DMA (which is the normal use case); that instance is auto-registered for
 * HAL DMA callbacks.
 */
class MAX22530 {
public:
    /**
     * @brief Construct a driver instance.
     *
     * @param hspi        HAL SPI handle (must be 8-bit, CPOL=0, CPHA=0).
     * @param cs_port     Chip-select GPIO port.
     * @param cs_pin      Chip-select GPIO pin (single bit).
     * @param int_port    Interrupt GPIO port.
     * @param int_pin     Interrupt GPIO pin (single bit, maps to EXTI line).
     * @param int_irqn    NVIC IRQ number for the EXTI line (e.g. EXTI1_IRQn).
     */
    MAX22530(SPI_HandleTypeDef* hspi,
             GPIO_TypeDef* cs_port, uint16_t cs_pin,
             GPIO_TypeDef* int_port, uint16_t int_pin,
             IRQn_Type int_irqn);

    /** @brief Initialize the chip, comparators, and interrupt line. */
    bool init();

    /** @brief Hard reset the field-side and logic-side (CONTROL.REST). */
    bool reset();

    /** @brief Soft reset the logic-side registers (CONTROL.SRES). */
    bool softReset();

    /** @brief Clear the power-on-reset flag (CONTROL.CLRPOR). */
    bool clearPOR();

    /**
     * @brief Clear the rolling-average filter for one channel.
     * @param channel 0..3
     */
    bool clearFilter(uint8_t channel);

    /** @brief Enable or disable SPI CRC (CONTROL.ENCRC). */
    bool enableCRC(bool enable);

    /**
     * @brief Low-level single-register read.
     *
     * Blocks with a timeout.  CRC must be disabled.
     */
    bool readRegister(uint8_t reg, uint16_t& out);

    /**
     * @brief Low-level single-register write.
     *
     * Blocks with a timeout.  CRC must be disabled.
     */
    bool writeRegister(uint8_t reg, uint16_t value);

    /** @brief Read one raw ADC channel as 12-bit counts. */
    uint16_t readRawCounts(uint8_t channel);

    /** @brief Read one filtered ADC channel as 12-bit counts. */
    uint16_t readFilteredCounts(uint8_t channel);

    /** @brief Read one raw ADC channel as volts at the MAX22530 input. */
    float readRawVoltage(uint8_t channel);

    /** @brief Read one filtered ADC channel as volts at the MAX22530 input. */
    float readFilteredVoltage(uint8_t channel);

    /**
     * @brief Blocking burst read of raw ADC1..4.
     *
     * @param out_counts    Output array of four 12-bit values.
     * @param int_status    Optional pointer to receive INTERRUPT_STATUS.
     * @return true on success.
     */
    bool burstReadRaw(uint16_t out_counts[4], uint16_t* int_status = nullptr);

    /**
     * @brief Blocking burst read of filtered FADC1..4.
     *
     * @param out_counts    Output array of four 12-bit values.
     * @param int_status    Optional pointer to receive INTERRUPT_STATUS.
     * @return true on success.
     */
    bool burstReadFiltered(uint16_t out_counts[4], uint16_t* int_status = nullptr);

    /**
     * @brief Configure a comparator window for a channel.
     *
     * Voltages are at the MAX22530 input (0..1.8 V), not the high-side scaled
     * value.  The default mode is digital-status (out-of-window) using the
     * filtered result.
     *
     * @param channel            0..3
     * @param high_v             Upper threshold [V].
     * @param low_v              Lower threshold [V].
     * @param use_filtered       true = compare FADC, false = compare raw ADC.
     * @param digital_status     true = out-of-window fault, false = hysteretic input.
     * @return true on success.
     */
    bool setComparatorThreshold(uint8_t channel, float high_v, float low_v,
                                bool use_filtered = true,
                                bool digital_status = true,
                                bool enable_pos_interrupt = true,
                                bool enable_neg_interrupt = true);

    /** @brief Read the COUT_STATUS register. */
    bool getComparatorStatus(uint16_t& status);

    /** @brief EXTI ISR callback.  Starts the SPI DMA burst read. */
    void onInterrupt();

    /** @brief DMA completion callback.  Parses the received burst. */
    void onDmaComplete();

    /** @brief DMA error callback.  Releases the chip-select. */
    void onDmaError();

    /** @brief Main-loop housekeeping.  Clears the new-sample flag. */
    void update();

    /** @brief True if at least one new sample is waiting to be read. */
    bool dataReady() const { return m_data_ready; }

    /** @brief Latest converted voltages [V] for the four channels. */
    float voltage(uint8_t channel) const {
        return (channel < 4) ? m_voltages[channel] : 0.0f;
    }

    /** @brief Last INTERRUPT_STATUS word latched from a DMA burst read. */
    uint16_t lastInterruptStatus() const { return m_int_status; }

    /** @brief Diagnostic counters. */
    uint32_t irqCount() const          { return m_irq_cnt; }
    uint32_t dmaCompleteCount() const  { return m_dma_cnt; }
    uint32_t dmaErrorCount() const     { return m_err_cnt; }
    uint32_t dmaStartFailCount() const { return m_dma_start_fail_cnt; }
    uint32_t crcErrorCount() const     { return m_crc_error_cnt; }
    bool     dmaBusy() const           { return m_dma_busy; }

    static MAX22530* instanceForPin(uint16_t pin);

public:
    static constexpr uint8_t MAX_BURST_LEN = 12U; // 11 bytes without CRC, 12 with CRC

private:
    bool resetInternal(uint16_t control_bits);
    void updateDmaTxBuffer();
    bool burstTransaction(uint8_t start_reg, uint16_t out_counts[4], uint16_t* int_status);
    bool parseBurst(const uint8_t* rx, uint8_t len);
    void raiseFaultsFromInterruptStatus(uint16_t status);
    uint16_t voltageToCounts(float v) const;
    float countsToVoltage(uint16_t counts) const;

    SPI_HandleTypeDef* m_hspi;
    GPIO_TypeDef*      m_cs_port;
    uint16_t           m_cs_pin;
    GPIO_TypeDef*      m_int_port;
    uint16_t           m_int_pin;
    IRQn_Type          m_int_irqn;

    volatile bool      m_data_ready = false;
    volatile bool      m_dma_busy   = false;
    volatile float     m_voltages[4] = {};
    volatile uint16_t  m_int_status = 0;
    bool               m_crc_enabled = false;
    uint8_t            m_burst_len = 11U;

    volatile uint32_t  m_irq_cnt            = 0;
    volatile uint32_t  m_dma_cnt            = 0;
    volatile uint32_t  m_err_cnt            = 0;
    volatile uint32_t  m_dma_start_fail_cnt = 0;
    volatile uint32_t  m_crc_error_cnt      = 0;

    /* Consecutive SPI/CRC/ DMA error counter.  A single noise glitch is ignored;
     * only a run of errors raises the latched Max22530Comm fault. */
    uint8_t            m_comm_err_seq       = 0;
    static constexpr uint8_t COMM_ERR_THRESHOLD = 3U;
};

} // namespace Inverter
