#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Time-based modulation ramp that pauses if phase current is too high.
 *
 * The ramp extends its own timeline while paused so it does not jump forward
 * when current drops.  If current stays high for too long the ramp aborts.
 */
class CurrentLimitedRamp {
public:
    enum class Status {
        RUNNING,
        DONE,
        ABORTED
    };

    CurrentLimitedRamp() = default;

    /**
     * @param from          Starting modulation index.
     * @param to            Target modulation index.
     * @param duration_ms   Total ramp time (excluding pauses).
     * @param current_limit_a Abort/pause threshold [A].  <=0 disables current limit.
     */
    void start(float from, float to, uint32_t duration_ms, float current_limit_a);

    /**
     * @param now_ms Current tick count.
     * @return RUNNING while ramping, DONE when target reached, ABORTED on over-current timeout.
     */
    Status update(uint32_t now_ms);

    /** Modulation index to apply right now. */
    float applied() const { return m_applied; }

    /** Target modulation index. */
    float target() const { return m_to; }

private:
    float m_from = 0.0f;
    float m_to = 0.0f;
    uint32_t m_duration_ms = 0;
    float m_current_limit = 0.0f;

    uint32_t m_start_ms = 0;
    float m_applied = 0.0f;

    bool m_paused = false;
    uint32_t m_pause_start_ms = 0;
};

} // namespace Inverter
