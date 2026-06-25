#pragma once

#include <cstdint>

#include "main.h"
#include "spi.h"

namespace Inverter {

/**
 * @brief Driver for the MAX22530/MAX22531/MAX22532 4-channel isolated ADC.
 *
 * The driver is pin-agnostic: pass the SPI handle, chip-select GPIO, and
 * hardware-interrupt GPIO in the constructor so multiple chips can coexist.
 *
 * The interrupt pin is configured as a falling-edge EXTI line (INT is
 * open-drain active-low).  The EXTI ISR starts a SPI DMA burst read; a DMA
 * completion callback parses the result and clears the interrupt status
 * register so the next end-of-conversion event can trigger.  update() in the
 * main loop just acknowledges the new-sample flag.
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

    /**
     * @brief Initialize the chip and interrupt line.
     *
     * Verifies the product ID, enables the end-of-conversion interrupt,
     * configures the EXTI input, and enables the NVIC line at low priority.
     *
     * @return true if the device responded with the expected product ID.
     */
    bool init();

    /**
     * @brief EXTI ISR callback.  Starts the SPI DMA burst read.
     */
    void onInterrupt();

    /**
     * @brief DMA completion callback.  Parses the received burst and releases
     * the chip-select.
     */
    void onDmaComplete();

    /**
     * @brief DMA error callback.  Releases the chip-select.
     */
    void onDmaError();

    /**
     * @brief Main-loop housekeeping.  Clears the new-sample flag.
     */
    void update();

    /**
     * @brief True if at least one new sample is waiting to be read.
     */
    bool dataReady() const { return m_data_ready; }

    /**
     * @brief Latest converted voltages [V] for the four channels.
     */
    float voltage(uint8_t channel) const {
        return (channel < 4) ? m_voltages[channel] : 0.0f;
    }

    /**
     * @brief Diagnostic counters (for debugging zero-read issues).
     */
    uint32_t irqCount() const          { return m_irq_cnt; }
    uint32_t dmaCompleteCount() const  { return m_dma_cnt; }
    uint32_t dmaErrorCount() const     { return m_err_cnt; }
    uint32_t dmaStartFailCount() const { return m_dma_start_fail_cnt; }
    bool     dmaBusy() const           { return m_dma_busy; }

    static MAX22530* instanceForPin(uint16_t pin);

private:
    bool readRegister(uint8_t reg, uint16_t& out);
    bool writeRegister(uint8_t reg, uint16_t value);

    SPI_HandleTypeDef* m_hspi;
    GPIO_TypeDef*      m_cs_port;
    uint16_t           m_cs_pin;
    GPIO_TypeDef*      m_int_port;
    uint16_t           m_int_pin;
    IRQn_Type          m_int_irqn;

    volatile bool      m_data_ready = false;
    volatile bool      m_dma_busy   = false;
    volatile float     m_voltages[4] = {};

    volatile uint32_t  m_irq_cnt            = 0;
    volatile uint32_t  m_dma_cnt            = 0;
    volatile uint32_t  m_err_cnt            = 0;
    volatile uint32_t  m_dma_start_fail_cnt = 0;
};

} // namespace Inverter
