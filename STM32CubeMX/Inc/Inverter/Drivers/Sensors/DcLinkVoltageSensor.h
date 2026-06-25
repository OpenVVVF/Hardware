#pragma once

#include "Inverter/Drivers/Sensors/MAX22530.h"

namespace Inverter {

/**
 * @brief High-voltage DC-link voltage sensor using the isolated ADC.
 *
 * Reads channel 1 of the MAX22530, applies a fixed divider scale, and
 * subtracts a zero-calibration offset.  The scaled voltage is published
 * to telemetry.
 */
class DcLinkVoltageSensor {
public:
    /**
     * @param adc            MAX22530 isolated ADC driver.
     * @param telemetry_key  Key used for telemetry, e.g. "vdc_v".
     * @param scale          Voltage-divider ratio from raw ADC volts to
     *                       high-side volts (default 1501.5f).
     */
    DcLinkVoltageSensor(MAX22530& adc,
                        const char* telemetry_key = "vdc_v",
                        float scale = 1501.5f);

    /**
     * @brief Initialize the underlying ADC.
     * @return true if the ADC responded.
     */
    bool init();

    /**
     * @brief Service the ADC and publish the scaled voltage.
     *
     * Call periodically from the main loop.
     */
    void update();

    /**
     * @brief Latest scaled voltage [V], or 0 if no sample yet.
     */
    float voltage() const { return m_voltage; }

    /**
     * @brief Capture the current raw ADC voltage as the zero offset.
     *
     * After calling this, voltage() will report 0 V for the present input.
     * Returns false if no valid sample is available.
     */
    bool zeroCalibrate();

private:
    MAX22530&   m_adc;
    const char* m_key;
    float       m_scale;
    float       m_zero_offset_v;
    float       m_voltage;
    bool        m_initialized;
    bool        m_has_sample;
};

/**
 * @brief Global DC-link voltage sensor instance.
 */
DcLinkVoltageSensor& dcLinkVoltageSensor();

} // namespace Inverter
