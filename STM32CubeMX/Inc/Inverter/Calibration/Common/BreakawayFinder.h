#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Ramps modulation until the encoder shows movement.
 *
 * Once the shaft has moved through detect_cycles, the finder boosts modulation
 * by torque_margin and reports FOUND.  If modulation reaches max_mod and the
 * shaft stalls for stall_timeout_ms, it reports TIMEOUT.
 */
class BreakawayFinder {
public:
    enum class Status {
        RUNNING,
        FOUND,
        TIMEOUT
    };

    BreakawayFinder() = default;

    void start(float step, uint32_t period_ms, float max_mod,
               float detect_cycles, float torque_margin,
               uint32_t stall_timeout_ms);

    /** Reset the movement reference without changing ramp parameters. */
    void resetReference();

    /**
     * @param now_ms              Current tick count.
     * @param encoder_moved_cycles Mechanical cycles moved since start.
     * @param out_mod             Modulation to apply this tick.
     * @return RUNNING, FOUND, or TIMEOUT.
     */
    Status update(uint32_t now_ms, float encoder_moved_cycles, float& out_mod);

    float breakawayMod() const { return m_breakaway_mod; }

private:
    float m_step = 0.0f;
    uint32_t m_period_ms = 0;
    float m_max_mod = 0.0f;
    float m_detect_cycles = 0.0f;
    float m_torque_margin = 1.0f;
    uint32_t m_stall_timeout_ms = 0;

    float m_mod = 0.0f;
    float m_breakaway_mod = 0.0f;
    uint32_t m_last_ramp_ms = 0;
    uint32_t m_last_move_ms = 0;
    float m_last_cycles = 0.0f;
    float m_start_cycles = 0.0f;
    bool  m_have_start_cycles = false;
};

} // namespace Inverter
