#include "Inverter/Calibration/Common/BreakawayFinder.h"

#include "Inverter/Telemetry.h"

#include <cmath>

namespace Inverter {

void BreakawayFinder::start(float step, uint32_t period_ms, float max_mod,
                            float detect_cycles, float torque_margin,
                            uint32_t stall_timeout_ms) {
    m_step = step;
    m_period_ms = period_ms;
    m_max_mod = max_mod;
    m_detect_cycles = detect_cycles;
    m_torque_margin = torque_margin;
    m_stall_timeout_ms = stall_timeout_ms;

    m_mod = 0.0f;
    m_breakaway_mod = 0.0f;
    m_last_ramp_ms = 0;  /* set on first update */
    m_last_move_ms = 0;
    m_last_cycles = 0.0f;
    resetReference();
}

void BreakawayFinder::resetReference() {
    m_start_cycles = 0.0f;
    m_have_start_cycles = false;
}

BreakawayFinder::Status BreakawayFinder::update(uint32_t now_ms, float encoder_moved_cycles, float& out_mod) {
    if (m_last_ramp_ms == 0U) {
        m_last_ramp_ms = now_ms;
        m_last_move_ms = now_ms;
    }

    /* Capture the first reported position as the zero reference so an
     * incorrect initial encoder reference does not look like movement. */
    if (!m_have_start_cycles) {
        m_start_cycles = encoder_moved_cycles;
        m_have_start_cycles = true;
        m_last_cycles = encoder_moved_cycles;
    }

    const float moved_cycles = encoder_moved_cycles - m_start_cycles;

    if (std::fabs(moved_cycles - m_last_cycles) > 0.01f) {
        m_last_cycles = moved_cycles;
        m_last_move_ms = now_ms;
    }

    if (std::fabs(moved_cycles) >= m_detect_cycles) {
        m_breakaway_mod = m_mod;
        Telemetry::printf("[CAL] RAMP: breakaway FOUND at mod=%.3f moved=%.3f detect=%.3f",
                          static_cast<double>(m_mod),
                          static_cast<double>(moved_cycles),
                          static_cast<double>(m_detect_cycles));
        float boosted = m_mod * m_torque_margin;
        if (boosted > m_max_mod) {
            boosted = m_max_mod;
        }
        m_mod = boosted;
        out_mod = m_mod;
        return Status::FOUND;
    }

    if ((now_ms - m_last_ramp_ms) >= m_period_ms) {
        m_last_ramp_ms = now_ms;
        m_mod += m_step;
        if (m_mod > m_max_mod) {
            m_mod = m_max_mod;
        }
    }

    out_mod = m_mod;

    if (m_mod >= m_max_mod && (now_ms - m_last_move_ms) > m_stall_timeout_ms) {
        Telemetry::printf("[CAL] RAMP: breakaway TIMEOUT at mod=%.3f moved=%.3f",
                          static_cast<double>(m_mod),
                          static_cast<double>(moved_cycles));
        return Status::TIMEOUT;
    }

    return Status::RUNNING;
}

} // namespace Inverter
