#include "Inverter/Drivers/Sensors/DcLinkCurrentSensor.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "adc.h"

#include <cmath>

namespace Inverter {

namespace {

DcLinkCurrentSensor s_instance;

/* ADC channels (see schematic: DC_LINK_CURSENS on PF11, _REF on PF12). */
constexpr uint32_t SIG_CHANNEL = ADC_CHANNEL_2;   /* ADC1_INP2 */
constexpr uint32_t REF_CHANNEL = ADC_CHANNEL_6;   /* ADC1_INP6 */

/* Same LA37S600 scaling as the phase-current sensors. */
constexpr uint32_t ADC_BITS       = 16;
constexpr float    ADC_VREF       = 3.3f;
constexpr float    DIVIDER        = 2.0f / 3.0f;
constexpr float    SENSITIVITY_VA = 1.042e-3f;

constexpr uint32_t SAMPLE_TIME = ADC_SAMPLETIME_64CYCLES_5;
constexpr uint32_t POLL_TIMEOUT_MS = 2U;
constexpr uint32_t ZERO_AVG = 64U;

} // namespace

DcLinkCurrentSensor& DcLinkCurrentSensor::instance() {
    return s_instance;
}

DcLinkCurrentSensor& dcLinkCurrentSensor() {
    return DcLinkCurrentSensor::instance();
}

bool DcLinkCurrentSensor::init() {
    /* Single conversions only (scan length stays 1): re-initialising the ADC
     * or lengthening the regular sequence would fight the live injected
     * phase-current stream.  And never HAL_ADC_Stop: that stops injected
     * conversions too. */
    if (!configChannel(SIG_CHANNEL)) {
        return false;
    }

    /* Zero offset: at boot the gate driver is held in reset, so the DC link
     * draw is just the small control supply; capture it as the zero point. */
    return zeroCalibrate();
}

bool DcLinkCurrentSensor::configChannel(uint32_t channel) {
    ADC_ChannelConfTypeDef cfg = {};
    cfg.Channel = channel;
    cfg.Rank = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = SAMPLE_TIME;
    cfg.SingleDiff = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    cfg.Offset = 0;
    return HAL_ADC_ConfigChannel(&hadc1, &cfg) == HAL_OK;
}

bool DcLinkCurrentSensor::zeroCalibrate() {
    double acc = 0.0;
    uint32_t good = 0;
    for (uint32_t i = 0; i < ZERO_AVG; ++i) {
        if (readPair()) {
            acc += countsToCurrent(m_raw_sig, m_raw_ref);
            ++good;
        }
    }
    if (good >= ZERO_AVG / 2U) {
        m_offset_a = static_cast<float>(acc / static_cast<double>(good));
        m_offset_valid = (std::fabs(m_offset_a) < 50.0f);
        m_energy_wh = 0.0f;
        m_last_energy_ms = HAL_GetTick();
    } else {
        m_offset_valid = false;
    }
    Telemetry::printf("[DCL] zero offset=%.3f A (%s, %lu/%lu samples)",
                      static_cast<double>(m_offset_a),
                      m_offset_valid ? "ok" : "SUSPECT",
                      static_cast<unsigned long>(good),
                      static_cast<unsigned long>(ZERO_AVG));
    return m_offset_valid;
}

float DcLinkCurrentSensor::countsToCurrent(uint32_t sig, uint32_t ref) const {
    const float lsb   = ADC_VREF / static_cast<float>((1U << ADC_BITS) - 1U);
    const float scale = lsb / (DIVIDER * SENSITIVITY_VA);
    return (static_cast<float>(sig) - static_cast<float>(ref)) * scale;
}

bool DcLinkCurrentSensor::readPair() {
    /* Two sequential single conversions, reconfiguring rank 1 between them.
     * No HAL_ADC_Stop anywhere: it would kill the injected phase stream. */
    if (!configChannel(SIG_CHANNEL)) return false;
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return false;
    if (HAL_ADC_PollForConversion(&hadc1, POLL_TIMEOUT_MS) != HAL_OK) return false;
    m_raw_sig = HAL_ADC_GetValue(&hadc1);

    if (!configChannel(REF_CHANNEL)) return false;
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return false;
    if (HAL_ADC_PollForConversion(&hadc1, POLL_TIMEOUT_MS) != HAL_OK) return false;
    m_raw_ref = HAL_ADC_GetValue(&hadc1);
    return true;
}

void DcLinkCurrentSensor::update() {
    const uint32_t now_ms = HAL_GetTick();
    if ((now_ms - m_last_poll_ms) < POLL_MS) {
        return;
    }
    m_last_poll_ms = now_ms;

    if (!readPair()) {
        return;
    }

    m_current_a = countsToCurrent(m_raw_sig, m_raw_ref) - m_offset_a;
    if (!m_offset_valid) {
        m_current_a = 0.0f;
    }

    m_power_w = dcLinkVoltageSensor().voltage() * m_current_a;

    /* Energy integral. */
    if (m_last_energy_ms != 0U) {
        const float dt_h = (now_ms - m_last_energy_ms) / 3600000.0f;
        if (dt_h > 0.0f && dt_h < 0.01f) {
            m_energy_wh += m_power_w * dt_h;
        }
    }
    m_last_energy_ms = now_ms;

    Telemetry::log("dcl_i_a", m_current_a);
    Telemetry::log("dcl_p_w", m_power_w);
    Telemetry::log("dcl_e_wh", m_energy_wh);
}

} // namespace Inverter
