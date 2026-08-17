#include "Inverter/Drivers/Sensors/DcLinkCurrentSensor.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Telemetry.h"

#include "main.h"

#include <cmath>

namespace Inverter {

namespace {

DcLinkCurrentSensor s_instance;

/* Same LA37S600 scaling as the phase-current sensors. */
constexpr uint32_t ADC_BITS       = 16;
constexpr float    ADC_VREF       = 3.3f;
constexpr float    DIVIDER        = 2.0f / 3.0f;
constexpr float    SENSITIVITY_VA = 1.042e-3f;

constexpr uint32_t ZERO_SAMPLES = 2000U;

} // namespace

DcLinkCurrentSensor& DcLinkCurrentSensor::instance() {
    return s_instance;
}

DcLinkCurrentSensor& dcLinkCurrentSensor() {
    return DcLinkCurrentSensor::instance();
}

bool DcLinkCurrentSensor::init() {
    /* Zero offset is captured from the injected stream (PWM-synchronized
     * ranks 3/4 on ADC1) once it has been running long enough to settle. */
    m_zero_samples_left = ZERO_SAMPLES;
    m_zero_acc = 0.0;
    m_offset_valid = false;
    m_offset_a = 0.0f;
    m_energy_wh = 0.0f;
    m_last_energy_ms = HAL_GetTick();
    return true;
}

bool DcLinkCurrentSensor::zeroCalibrate() {
    return init();
}

float DcLinkCurrentSensor::countsToCurrent(uint32_t sig, uint32_t ref) const {
    const float lsb   = ADC_VREF / static_cast<float>((1U << ADC_BITS) - 1U);
    const float scale = lsb / (DIVIDER * SENSITIVITY_VA);
    return (static_cast<float>(sig) - static_cast<float>(ref)) * scale;
}

void DcLinkCurrentSensor::update() {
    /* Disabled: the injected rank 3/4 approach degraded the phase-current
     * path, and polled regular conversions alias the sensor supply ripple.
     * DC-link current needs its own clean sampling path (see reminder.md). */
    return;
}

} // namespace Inverter

