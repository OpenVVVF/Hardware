#pragma once

#include "adc.h"
#include <cstdint>

namespace Inverter {

/**
 * @brief One ADC channel used by a current sensor.
 */
struct CurrentSensorAdcChannel {
    ADC_HandleTypeDef* hadc;
    uint32_t channel;
};

/**
 * @brief Configuration for one LA37SxxxS05K(M) style current sensor.
 *
 * The output (Vout) and reference (Vref) pins are both divided by the same
 * resistor divider.  If the reference channel uses a different ADC than the
 * output channel, the two voltages are sampled simultaneously (dual-ADC mode).
 * If they share an ADC, they are sampled sequentially.
 */
struct CurrentSensorConfig {
    const char* name;

    CurrentSensorAdcChannel output;   /**< Vout ADC channel. */
    CurrentSensorAdcChannel reference; /**< Vref ADC channel. If hadc == nullptr,
                                            the output ADC is used twice. */

    float vref_mcu;          /**< MCU ADC reference voltage, e.g. 3.3 V. */
    uint32_t adc_bits;       /**< ADC resolution: 16 for ADC1/ADC2, 12 for ADC3. */

    float voltage_divider;   /**< Resistor divider ratio applied to both signals,
                                  e.g. 2.0f / 3.0f. */
    float sensitivity_va;    /**< Sensor sensitivity in V/A *after* the divider,
                                  e.g. 0.5208e-3 for LA37S600 (bipolar 625 mV span). */
};

/**
 * @brief Single current-sensor abstraction with voltage-divider compensation.
 */
class CurrentSensor {
public:
    CurrentSensor() = default;

    void init(const CurrentSensorConfig& cfg);

    /**
     * @brief Convert raw ADC codes to current/voltages.
     *
     * Does not perform any ADC I/O; useful when the samples were captured
     * outside of this class (e.g. during a coordinated multi-phase snapshot).
     */
    void computeFromRaw(uint32_t raw_out, uint32_t raw_ref,
                        float& current_a, float& vout_v, float& vref_v) const;

    /**
     * @brief Sample this sensor by itself.
     *
     * Returns sensor-side voltages (before the divider) and the computed current.
     * Current is (vout - vref) / sensitivity_va.
     */
    bool read(float& current_a, float& vout_v, float& vref_v);

    const char* name() const { return cfg_.name; }

private:
    CurrentSensorConfig cfg_ = {};

    bool configureChannel(ADC_HandleTypeDef* hadc, uint32_t channel);
    uint32_t readSingleAdc(ADC_HandleTypeDef* hadc);
    float rawToVoltage(uint32_t raw) const;
};

} // namespace Inverter
