#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"

#include "Inverter/Telemetry.h"
#include "main.h"

namespace Inverter {

namespace {

/* Default scaled-voltage offset observed when the high-voltage input is at
 * 0 V.  This is converted to raw ADC volts using the configured scale. */
constexpr float DEFAULT_VZERO_OFFSET_SCALED_V = 0;

/* Global hardware instance.  Construction only stores pointers; HAL calls
 * happen later in init(). */
MAX22530 s_adc(
    &hspi2,
    SPI2_CS_GPIO_Port, SPI2_CS_Pin,
    VSENSE_ISO_ADC_INTERRUPT_GPIO_Port, VSENSE_ISO_ADC_INTERRUPT_Pin,
    EXTI1_IRQn);

DcLinkVoltageSensor s_vdc(s_adc);

} // namespace

DcLinkVoltageSensor& dcLinkVoltageSensor() {
    return s_vdc;
}

DcLinkVoltageSensor::DcLinkVoltageSensor(MAX22530& adc,
                                         const char* telemetry_key,
                                         float scale)
    : m_adc(adc),
      m_key(telemetry_key ? telemetry_key : "vdc_v"),
      m_scale(scale),
      m_zero_offset_v(DEFAULT_VZERO_OFFSET_SCALED_V / scale),
      m_voltage(0.0f),
      m_ov_threshold_v(1.9f * scale),   /* raw input > 1.8 V: effectively disabled */
      m_uv_threshold_v(-0.1f * scale),  /* raw input < 0 V: effectively disabled */
      m_initialized(false),
      m_has_sample(false) {
}

bool DcLinkVoltageSensor::init() {
    m_initialized = m_adc.init();
    if (m_initialized) {
        (void)applyComparatorThresholds();
    }
    return m_initialized;
}

bool DcLinkVoltageSensor::setOvervoltageThreshold(float v) {
    m_ov_threshold_v = v;
    return applyComparatorThresholds();
}

bool DcLinkVoltageSensor::setUndervoltageThreshold(float v) {
    m_uv_threshold_v = v;
    return applyComparatorThresholds();
}

bool DcLinkVoltageSensor::applyComparatorThresholds() {
    /* Convert scaled high-side volts to raw MAX22530 input volts. */
    const float raw_ov = m_ov_threshold_v / m_scale;
    const float raw_uv = m_uv_threshold_v / m_scale;

    /* Only enable the comparator interrupt direction if the threshold is
     * actually inside the ADC range.  The default OV/UV thresholds are placed
     * outside the 0..1.8 V input range to disable them; without this, a
     * clamped low threshold of 0 counts could falsely trip UV when Vbus sags. */
    constexpr float VREF = 1.8f;
    const bool enable_ov = (raw_ov > 0.0f && raw_ov < VREF);
    const bool enable_uv = (raw_uv > 0.0f && raw_uv < VREF);

    /* Digital-status mode with filtered input; use channel 1 (index 0). */
    return m_adc.setComparatorThreshold(0, raw_ov, raw_uv, true, true,
                                        enable_ov, enable_uv);
}

void DcLinkVoltageSensor::update() {
    /* A true dataReady() means the ISR has collected a new ADC sample since
     * the last loop iteration. */
    const bool had_new = m_adc.dataReady();
    m_adc.update();

    if (had_new) {
        m_has_sample = true;
    }

    if (m_has_sample) {
        /* voltage(0) returns the latest converted voltage at the MAX22530 input. */
        const float raw_v = m_adc.voltage(0);
        m_voltage = (raw_v - m_zero_offset_v) * m_scale;
    }

    Telemetry::log(m_key, m_voltage);
}

bool DcLinkVoltageSensor::zeroCalibrate() {
    /* Make sure we have the freshest sample possible. */
    m_adc.update();

    if (!m_has_sample) {
        return false;
    }

    m_zero_offset_v = m_adc.voltage(0);
    return true;
}

} // namespace Inverter
