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
        m_last_ms = now_ms;
    }

    if (m_duration_ms == 0U || std::fabs(m_from - m_to) < 1e-4f) {
        m_applied = m_to;
        return Status::DONE;
    }

    const float dt = (now_ms - m_last_ms) * 1.0e-3f;
    m_last_ms = now_ms;

    uint32_t elapsed = now_ms - m_start_ms;
    if (elapsed > m_duration_ms) {
        elapsed = m_duration_ms;
    }

    const float desired = m_from + (m_to - m_from) *
                        static_cast<float>(elapsed) / static_cast<float>(m_duration_ms);

    if (m_current_limit > 0.0f && dt > 0.0f && dt < 0.5f) {
        const float i_max = maxPhaseCurrentMagnitude();

        if (i_max > m_current_limit) {
            /* Throttle down instead of pausing/aborting: at high bus voltage
             * the demanded modulation would draw far too much current, so
             * back the applied modulation off fast and let the rotation
             * continue at the current limit.  Floored at half the target so
             * a transient spike cannot kill the rotation entirely. */
            if (!m_paused) {
                m_paused = true;
                Telemetry::printf("[CAL] RAMP: throttled: I=%.1f A limit=%.1f A",
                                  static_cast<double>(i_max),
                                  static_cast<double>(m_current_limit));
            }
            const float floor_mod = 0.5f * m_to;
            const float decay = 1.0f - std::exp(-dt / 0.020f);
            m_applied -= m_applied * decay;
            if (m_applied < floor_mod) m_applied = floor_mod;
        } else {
            if (m_paused && i_max <= 0.8f * m_current_limit) {
                m_paused = false;
                Telemetry::printf("[CAL] RAMP: released: I=%.1f A limit=%.1f A",
                                  static_cast<double>(i_max),
                                  static_cast<double>(m_current_limit));
            }
            /* Recover toward the ramp's desired value slowly (~0.5 s). */
            const float recover = 1.0f - std::exp(-dt / 0.5f);
            m_applied += (desired - m_applied) * recover;
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
