#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Automated pole-pair calibration.
 *
 * Runs the motor open-loop at 1 Hz, ramps modulation until the encoder turns,
 * then counts commanded electrical cycles against encoder mechanical cycles to
 * determine the pole-pair ratio.
 *
 * Movement is detected from the raw encoder angle (so partial rotation and
 * cogging vibration are handled).  Completed encoder cycles are counted from
 * the filtered sin/cos zero crossings, which is far more robust than integrating
 * the raw angle when the rotor vibrates near the wrap boundary.
 *
 * The reported ratio is the true pole-pair count only if the encoder sin/cos
 * produces one cycle per mechanical revolution.  Use encodercal to measure
 * encoder cycles per revolution if needed.
 */
class PolePairCalibrator {
public:
    PolePairCalibrator() = default;

    /**
     * @brief Start the calibration.
     *
     * The motor must be stopped.  Returns false if the gate driver cannot be
     * enabled or the open-loop controller cannot start.
     */
    bool start();

    /**
     * @brief Non-blocking state-machine update.  Call at ~100 Hz from the main
     * loop.
     */
    void update();

    /**
     * @brief True while a calibration is in progress.
     */
    bool isActive() const { return m_state != State::IDLE && m_state != State::DONE; }

    /**
     * @brief Most recent ratio, or 0 if no calibration has finished.
     */
    float lastRatio() const { return m_last_ratio; }

    static PolePairCalibrator& instance();

private:
    enum class State {
        IDLE,
        RAMP,
        COUNT,
        DONE,
        FAIL
    };

    void sampleEncoderAngle();
    float encoderCycles() const { return m_unwrapped_angle / 360.0f; }
    void reportRatio(const char* label);

    State    m_state = State::IDLE;
    float    m_mod = 0.0f;

    /* Breakaway detection. */
    bool     m_breakaway_detected = false;
    float    m_breakaway_mod = 0.0f;
    float    m_breakaway_mech_cycles = 0.0f;

    /* Encoder angle tracking (for movement/stall detection). */
    float    m_last_angle = 0.0f;
    float    m_unwrapped_angle = 0.0f;

    /* Mechanical cycle counter snapshot at count start. */
    float    m_mech_count_start = 0.0f;

    /* Electrical cycle counter snapshot at count start. */
    uint32_t m_elec_count_start = 0;

    uint32_t m_last_ramp_ms = 0;
    uint32_t m_count_start_ms = 0;
    uint32_t m_last_move_ms = 0;
    float    m_cycles_at_last_move = 0.0f;
    float    m_last_ratio = 0.0f;
};

PolePairCalibrator& polePairCalibrator();

} // namespace Inverter
