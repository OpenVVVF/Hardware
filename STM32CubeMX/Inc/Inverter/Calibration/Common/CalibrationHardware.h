#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Non-blocking gate-driver power/ready sequence for calibrators.
 *
 * Typical usage:
 *   - start() calls begin() and transitions to a WAIT_HARDWARE state.
 *   - update() calls m_hw.update() until isReady() or hasFailed() is true.
 *   - shutdown() is called on completion or failure.
 */
class CalibrationHardware {
public:
    enum class State {
        IDLE,
        POWER_STABILIZE,
        READY_WAIT,
        READY,
        FAILED
    };

    CalibrationHardware() = default;

    /** Start the power/ready sequence. */
    void begin();

    /** Step the non-blocking sequence.  Call at ~100 Hz. */
    void update();

    State state() const { return m_state; }
    bool isReady() const { return m_state == State::READY; }
    bool hasFailed() const { return m_state == State::FAILED; }

    /** Release gate-driver reset (enable outputs). */
    static void releaseOutputs();

    /** Assert gate-driver reset (disable outputs). */
    static void assertOutputs();

    /** Park all phases at 50 % duty. */
    static void parkOutputs();

    /** Stop SPWM, park outputs, and assert reset. */
    static void shutdown();

private:
    State m_state = State::IDLE;
    uint32_t m_state_start_ms = 0;
};

} // namespace Inverter
