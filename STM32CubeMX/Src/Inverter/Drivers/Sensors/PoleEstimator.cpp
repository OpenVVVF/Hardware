#include "Inverter/Drivers/Sensors/PoleEstimator.h"

#include <cmath>

namespace Inverter {

static PoleEstimator s_instance;

PoleEstimator& PoleEstimator::instance() {
    return s_instance;
}

void PoleEstimator::setElectricalFrequency(float f_hz) {
    m_f_elec_hz = (f_hz > 0.0f) ? f_hz : 0.0f;
}

void PoleEstimator::setEnabled(bool enabled, uint16_t raw_sin, uint16_t raw_cos) {
    if (enabled && !m_enabled) {
        reset();
        m_sin_offset = static_cast<float>(raw_sin);
        m_cos_offset = static_cast<float>(raw_cos);
    }
    m_enabled = enabled;
}

void PoleEstimator::reset() {
    m_sin_offset = 0.0f;
    m_cos_offset = 0.0f;
    m_sin_filt = 0.0f;
    m_cos_filt = 0.0f;
    m_iu_filt = 0.0f;
    m_mech_state = 0;
    m_prev_mech_state = 0;
    m_mech_cycles = 0.0f;
    m_manual_cal = false;
    m_manual_mech_cycles = 0.0f;
    m_current_state = 0;
    m_prev_current_state = 0;
    m_iu_peak = 0.0f;
    m_elec_cycles = 0.0f;
    for (size_t i = 0; i < WINDOW_CYCLES; ++i) {
        m_elec_at_mech[i] = 0.0f;
    }
    m_mech_window_idx = 0;
    m_window_poles = 0.0f;
    m_filtered_poles = 0.0f;
}

void PoleEstimator::startManualEncoderCal() {
    m_manual_mech_cycles = 0.0f;
    m_manual_cal = true;
    /* The cycle-counting code lives inside onSample(), which bails out early if
     * the estimator is disabled.  Force-enable it during a manual calibration so
     * encoder zero crossings are counted even though the motor is not running. */
    m_manual_prev_enabled = m_enabled;
    m_enabled = true;
}

void PoleEstimator::stopManualEncoderCal() {
    m_manual_cal = false;
    m_enabled = m_manual_prev_enabled;
}

void PoleEstimator::onSample(float iu, uint16_t raw_sin, uint16_t raw_cos) {
    if (!m_enabled) {
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

    /* Mechanical cycle detection: one positive zero crossing of sin per rev.
     * Use a large hysteresis (25 % of amplitude) so switching noise does not
     * create extra crossings. */
    const float mech_amp = std::sqrt(m_sin_filt * m_sin_filt + m_cos_filt * m_cos_filt);
    const float mech_threshold = std::max(100.0f, 0.25f * mech_amp);
    if (m_sin_filt > mech_threshold) {
        m_mech_state = 1;
    } else if (m_sin_filt < -mech_threshold) {
        m_mech_state = -1;
    }

    if (m_prev_mech_state < 0 && m_mech_state > 0) {
        m_mech_cycles += 1.0f;
        if (m_manual_cal) {
            m_manual_mech_cycles += 1.0f;
        }

        /* Store electrical cycle count at this mech cycle for the window. */
        m_elec_at_mech[m_mech_window_idx % WINDOW_CYCLES] = m_elec_cycles;
        ++m_mech_window_idx;

        /* Compute pole count over the last WINDOW_CYCLES revolutions. */
        if (m_mech_cycles >= static_cast<float>(WINDOW_CYCLES)) {
            const float elec_at_start = m_elec_at_mech[m_mech_window_idx % WINDOW_CYCLES];
            const float elec_in_window = m_elec_cycles - elec_at_start;
            if (elec_in_window > 0.5f) {
                m_window_poles = 2.0f * elec_in_window / static_cast<float>(WINDOW_CYCLES);
            }
        }
    }
    if (m_mech_state != 0) {
        m_prev_mech_state = m_mech_state;
    }

    /* Electrical cycle detection from phase-U current.
     * Heavier filtering keeps the fundamental zero crossing clean; the
     * threshold is derived from the average absolute value so a single spike
     * does not hold the hysteresis band high for seconds. */
    constexpr float CURRENT_ALPHA = 0.05f;
    m_iu_filt += CURRENT_ALPHA * (iu - m_iu_filt);

    constexpr float AVG_ABS_ALPHA = 0.01f;
    const float abs_iu = std::fabs(m_iu_filt);
    m_iu_peak += AVG_ABS_ALPHA * (abs_iu - m_iu_peak);

    const float curr_threshold = std::max(0.1f, 0.4f * m_iu_peak);
    if (m_iu_filt > curr_threshold) {
        m_current_state = 1;
    } else if (m_iu_filt < -curr_threshold) {
        m_current_state = -1;
    }

    if (m_prev_current_state < 0 && m_current_state > 0) {
        m_elec_cycles += 1.0f;
    }
    if (m_current_state != 0) {
        m_prev_current_state = m_current_state;
    }

    /* Choose the raw estimate.  Prefer the sliding-window cycle ratio once
     * enough revolutions have passed; fall back to the cumulative ratio before
     * that.  We intentionally skip the commanded-frequency hint here because
     * the cycle ratio is far more robust to encoder calibration errors. */
    float raw_poles = 0.0f;
    if (m_window_poles > 0.0f) {
        raw_poles = m_window_poles;
    } else if (m_mech_cycles > 0.5f && m_elec_cycles > 0.5f) {
        raw_poles = 2.0f * m_elec_cycles / m_mech_cycles;
    } else {
        return;
    }

    if (m_filtered_poles <= 0.0f) {
        m_filtered_poles = raw_poles;
    } else {
        /* ~100 ms time constant at 10 kHz.  Very smooth once the window is full. */
        constexpr float ESTIMATE_ALPHA = 0.001f;
        m_filtered_poles += ESTIMATE_ALPHA * (raw_poles - m_filtered_poles);
    }
}

} // namespace Inverter
