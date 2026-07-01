#include "Inverter/Calibration/Common/CurrentLimitedRamp.h"

#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Telemetry.h"

#include <cmath>

namespace Inverter {

static float maxPhaseCurrentMagnitude() {
    const float iu = phaseCurrentADC().lastU();
    const float iv = phaseCurrentADC().lastV();
    const float iw = -(iu + iv);
    float max_i = std::fabs(iu);
    if (std::fabs(iv) > max_i) {
        max_i = std::fabs(iv);
    }
    if (std::fabs(iw) > max_i) {
        max_i = std::fabs(iw);
    }
    return max_i;
}

void CurrentLimitedRamp::start(float from, float to, uint32_t duration_ms, float current_limit_a) {
    m_from = from;
    m_to = to;
    m_duration_ms = duration_ms;
    m_current_limit = current_limit_a;
    m_start_ms = 0;  /* set on first update */
    m_applied = from;
    m_paused = false;
    m_pause_start_ms = 0;
}

CurrentLimitedRamp::Status CurrentLimitedRamp::update(uint32_t now_ms) {
    if (m_start_ms == 0U) {
        m_start_ms = now_ms;
    }

    if (m_duration_ms == 0U || std::fabs(m_from - m_to) < 1e-4f) {
        m_applied = m_to;
        return Status::DONE;
    }

    uint32_t elapsed = now_ms - m_start_ms;
    if (elapsed > m_duration_ms) {
        elapsed = m_duration_ms;
    }

    float desired = m_from + (m_to - m_from) *
                        static_cast<float>(elapsed) / static_cast<float>(m_duration_ms);

    if (m_current_limit > 0.0f) {
        const float i_max = maxPhaseCurrentMagnitude();
        const float resume_threshold = 0.8f * m_current_limit;
        const bool trying_to_increase = (desired > m_applied);

        if (i_max > m_current_limit && trying_to_increase) {
            if (!m_paused) {
                m_paused = true;
                m_pause_start_ms = now_ms;
                Telemetry::printf("[CAL] RAMP: paused: I=%.1f A limit=%.1f A",
                                  static_cast<double>(i_max),
                                  static_cast<double>(m_current_limit));
            }
            desired = m_applied;
        } else if (m_paused && i_max <= resume_threshold) {
            m_start_ms += (now_ms - m_pause_start_ms);
            m_paused = false;
            m_applied = desired;
            Telemetry::printf("[CAL] RAMP: resumed: I=%.1f A limit=%.1f A",
                              static_cast<double>(i_max),
                              static_cast<double>(m_current_limit));
        } else if (!m_paused) {
            m_applied = desired;
        }

        if (m_paused && (now_ms - m_pause_start_ms) > 200U) {
            Telemetry::printf("[CAL] RAMP: ABORTED: current stayed above limit");
            return Status::ABORTED;
        }
    } else {
        m_applied = desired;
    }

    if (elapsed >= m_duration_ms && !m_paused) {
        m_applied = m_to;
        return Status::DONE;
    }

    return Status::RUNNING;
}

} // namespace Inverter
