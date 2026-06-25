#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"

#include "Inverter/Telemetry.h"
#include "main.h"

namespace Inverter {

namespace {

/* Default scaled-voltage offset observed when the high-voltage input is at
 * 0 V.  This is converted to raw ADC volts using the configured scale. */
constexpr float DEFAULT_VZERO_OFFSET_SCALED_V = 9.898f;

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
      m_initialized(false),
      m_has_sample(false) {
}

bool DcLinkVoltageSensor::init() {
    m_initialized = m_adc.init();
    return m_initialized;
}

void DcLinkVoltageSensor::update() {
    /* A true dataReady() here means update() will perform a fresh SPI burst read. */
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
