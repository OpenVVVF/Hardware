#pragma once

#include <cstdint>
#include <cstddef>

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
 * open-drain active-low).  The ISR only sets a ready flag; the SPI burst read
 * and telemetry publish are done from update() in thread/main-loop context.
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
     * @param name_prefix Telemetry key prefix (e.g. "iso" -> "iso_adc1_v").
     */
    MAX22530(SPI_HandleTypeDef* hspi,
             GPIO_TypeDef* cs_port, uint16_t cs_pin,
             GPIO_TypeDef* int_port, uint16_t int_pin,
             IRQn_Type int_irqn,
             const char* name_prefix);

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
     * @brief ISR callback.  Called from the EXTI HAL callback.
     */
    void onInterrupt();

    /**
     * @brief Non-blocking update.  Reads the latest ADC values if new data
     * arrived and publishes them over telemetry.
     *
     * Call periodically from the main loop.
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
     * @brief Raw interrupt status from the last burst read.
     */
    uint16_t interruptStatus() const { return m_int_status; }

    static MAX22530* instanceForPin(uint16_t pin);

private:
    bool readRegister(uint8_t reg, uint16_t& out);
    bool writeRegister(uint8_t reg, uint16_t value);
    bool burstReadAdc(uint16_t raw[4], uint16_t& status);
    void publish();

    SPI_HandleTypeDef* m_hspi;
    GPIO_TypeDef*      m_cs_port;
    uint16_t           m_cs_pin;
    GPIO_TypeDef*      m_int_port;
    uint16_t           m_int_pin;
    IRQn_Type          m_int_irqn;
    char               m_prefix[8];

    volatile bool      m_data_ready = false;
    volatile uint32_t  m_interrupt_count = 0;
    uint32_t           m_last_data_ms = 0;
    bool               m_first_data = true;
    bool               m_warned_no_int = false;
    float              m_voltages[4] = {};
    uint16_t           m_int_status = 0;
};

} // namespace Inverter
