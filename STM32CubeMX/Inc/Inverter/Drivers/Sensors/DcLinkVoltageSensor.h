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
                        float scale = 1516.0f);

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
     * @brief True once at least one valid sample has been received.
     */
    bool hasSample() const { return m_has_sample; }

    /**
     * @brief Capture the current raw ADC voltage as the zero offset.
     *
     * After calling this, voltage() will report 0 V for the present input.
     * Returns false if no valid sample is available.
     */
    bool zeroCalibrate();

    /**
     * @brief Set the scaled overvoltage threshold [V].
     *
     * The MAX22530 channel-1 comparator is configured in digital-status mode
     * (out-of-window) using the filtered ADC result.  A fault is raised when
     * vdc_v exceeds this value.
     */
    bool setOvervoltageThreshold(float v);

    /**
     * @brief Set the scaled undervoltage threshold [V].
     *
     * A fault is raised when vdc_v drops below this value.
     */
    bool setUndervoltageThreshold(float v);

    float overvoltageThreshold() const { return m_ov_threshold_v; }
    float undervoltageThreshold() const { return m_uv_threshold_v; }

    /** @brief Direct access to the underlying ADC driver for diagnostics. */
    MAX22530& adc() { return m_adc; }

private:
    bool applyComparatorThresholds();

    MAX22530&   m_adc;
    const char* m_key;
    float       m_scale;
    float       m_zero_offset_v;
    volatile float m_voltage;
    float       m_ov_threshold_v;
    float       m_uv_threshold_v;
    bool        m_initialized;
    bool        m_has_sample;
};

/**
 * @brief Global DC-link voltage sensor instance.
 */
DcLinkVoltageSensor& dcLinkVoltageSensor();

} // namespace Inverter
