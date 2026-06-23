#include "Inverter/Drivers/Sensors/PolePairEstimator.h"

#include <cmath>

namespace Inverter {

static PolePairEstimator s_instance;

PolePairEstimator& PolePairEstimator::instance() {
    return s_instance;
}

void PolePairEstimator::setEnabled(bool enabled) {
    if (enabled && !m_enabled) {
        reset();
    }
    m_enabled = enabled;
}

void PolePairEstimator::reset() {
    m_last_angle_deg = 0.0f;
    m_mech_deg_total = 0.0f;
    m_elec_cycles = 0.0f;
    m_current_state = 0;
    m_prev_state = 0;
    m_raw_pp = 0.0f;
    m_filtered_pp = 0.0f;
}

void PolePairEstimator::onSample(float iu, float enc_angle_deg) {
    if (!m_enabled) {
        return;
    }

    /* Unwrap mechanical encoder angle.  A sin/cos encoder gives 0..360 deg
     * per mechanical revolution; track total rotation to avoid wrap errors. */
    float delta = enc_angle_deg - m_last_angle_deg;
    if (delta > 180.0f) {
        delta -= 360.0f;
    } else if (delta < -180.0f) {
        delta += 360.0f;
    }
    m_mech_deg_total += delta;
    m_last_angle_deg = enc_angle_deg;

    /* Hysteretic zero-crossing detector on phase-U current.
     * One positive-going crossing = one electrical cycle. */
    constexpr float THRESHOLD_A = 0.05f;
    if (iu > THRESHOLD_A) {
        m_current_state = 1;
    } else if (iu < -THRESHOLD_A) {
        m_current_state = -1;
    }

    if (m_prev_state < 0 && m_current_state > 0) {
        m_elec_cycles += 1.0f;
    }
    if (m_current_state != 0) {
        m_prev_state = m_current_state;
    }

    /* Refine the estimate once we have rotated at least a quarter revolution.
     * Use absolute values so direction does not matter. */
    const float mech_revs = std::fabs(m_mech_deg_total) / 360.0f;
    if (mech_revs > 0.25f && m_elec_cycles > 0.5f) {
        m_raw_pp = std::fabs(m_elec_cycles) / mech_revs;

        if (m_filtered_pp <= 0.0f) {
            m_filtered_pp = m_raw_pp;
        } else {
            /* ~1 s time constant at 10 kHz sample rate. */
            constexpr float ALPHA = 0.0001f;
            m_filtered_pp += ALPHA * (m_raw_pp - m_filtered_pp);
        }
    }
}

} // namespace Inverter
