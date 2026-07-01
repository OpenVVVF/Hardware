#include "Inverter/Calibration/EncoderCycleCalibrator.h"

#include <cmath>
#include <algorithm>

namespace Inverter {

static EncoderCycleCalibrator s_instance;

EncoderCycleCalibrator& EncoderCycleCalibrator::instance() {
    return s_instance;
}

void EncoderCycleCalibrator::start() {
    reset();
    m_active = true;
}

void EncoderCycleCalibrator::stop() {
    m_active = false;
}

void EncoderCycleCalibrator::reset() {
    m_sin_offset = 0.0f;
    m_cos_offset = 0.0f;
    m_sin_filt   = 0.0f;
    m_cos_filt   = 0.0f;
    m_state      = 0;
    m_prev_state = 0;
    m_cycles     = 0.0f;
}

void EncoderCycleCalibrator::onSample(uint16_t raw_sin, uint16_t raw_cos) {
    if (!m_active) {
        return;
    }

    /* Refine DC offsets. */
    constexpr float OFFSET_ALPHA = 0.0002f;
    m_sin_offset += OFFSET_ALPHA * (static_cast<float>(raw_sin) - m_sin_offset);
    m_cos_offset += OFFSET_ALPHA * (static_cast<float>(raw_cos) - m_cos_offset);

    /* Low-pass filter sin/cos before zero-crossing detection.  Cutoff is well
     * above the fundamental frequency but removes PWM/switching ripple. */
    constexpr float SIGNAL_ALPHA = 0.2f;
    const float sin_raw = static_cast<float>(raw_sin) - m_sin_offset;
    const float cos_raw = static_cast<float>(raw_cos) - m_cos_offset;
    m_sin_filt += SIGNAL_ALPHA * (sin_raw - m_sin_filt);
    m_cos_filt += SIGNAL_ALPHA * (cos_raw - m_cos_filt);

    /* Mechanical cycle detection: one positive zero crossing of sin per
     * sin/cos period.  Use a large hysteresis (25 % of amplitude) so switching
     * noise does not create extra crossings. */
    const float mech_amp = std::sqrt(m_sin_filt * m_sin_filt + m_cos_filt * m_cos_filt);
    const float mech_threshold = std::max(100.0f, 0.25f * mech_amp);
    if (m_sin_filt > mech_threshold) {
        m_state = 1;
    } else if (m_sin_filt < -mech_threshold) {
        m_state = -1;
    }

    if (m_prev_state < 0 && m_state > 0) {
        m_cycles += 1.0f;
    }
    if (m_state != 0) {
        m_prev_state = m_state;
    }
}

} // namespace Inverter
