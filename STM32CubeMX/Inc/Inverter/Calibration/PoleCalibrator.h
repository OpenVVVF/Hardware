#pragma once

#include "Inverter/Calibration/Common/EncoderTracker.h"
#include "Inverter/Calibration/Common/BreakawayFinder.h"

#include <cstdint>

namespace Inverter {

/**
 * @brief Automated pole calibration.
 *
 * Runs the motor open-loop at 1 Hz, ramps modulation until the encoder turns,
 * then counts commanded electrical cycles against encoder mechanical cycles to
 * determine the pole count.
 *
 * Movement is detected from the raw encoder angle (so partial rotation and
 * cogging vibration are handled).  Completed encoder cycles are counted from
 * the filtered sin/cos zero crossings, which is far more robust than integrating
 * the raw angle when the rotor vibrates near the wrap boundary.
 *
 * The reported pole count is the true pole count only if the encoder sin/cos
 * produces one cycle per mechanical revolution.  Use encodercal to measure
 * encoder cycles per revolution if needed.
 */
class PoleCalibrator {
public:
    PoleCalibrator() = default;

    /**
     * @brief Start the calibration.
     *
     * The motor must be stopped.  Returns false if the gate driver cannot be
     * enabled or the open-loop controller cannot start.
     *
     * @param max_mod        Maximum modulation index used during breakaway ramp.
     * @param torque_margin  Multiplier applied to the found breakaway modulation
     *                       to ensure continuous rotation during counting.
     */
    bool start(float max_mod = 0.50f, float torque_margin = 1.30f);

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
     * @brief Abort a running calibration and return to idle.
     */
    void stop();

    /**
     * @brief Most recent pole count, or 0 if no calibration has finished.
     */
    float lastPoles() const { return m_last_poles; }

    /**
     * @brief Modulation at which the rotor broke away during the last run.
     */
    float lastBreakawayMod() const { return m_breakaway_mod; }

    static PoleCalibrator& instance();

private:
    enum class State {
        IDLE,
        RAMP,
        COUNT,
        DONE,
        FAIL
    };

    void reportPoles(const char* label);

    State    m_state = State::IDLE;
    float    m_mod = 0.0f;

    /* Breakaway detection. */
    BreakawayFinder m_breakaway;
    bool     m_breakaway_found = false;
    float    m_breakaway_mod = 0.0f;
    float    m_breakaway_mech_cycles = 0.0f;
    float    m_max_mod = 0.50f;

    /* Encoder angle tracking (for movement/stall detection). */
    EncoderTracker m_tracker;

    /* Mechanical cycle counter snapshot at count start. */
    float    m_mech_count_start = 0.0f;

    /* Electrical cycle counter snapshot at count start. */
    uint32_t m_elec_count_start = 0;

    uint32_t m_count_start_ms = 0;
    float    m_last_poles = 0.0f;
};

PoleCalibrator& poleCalibrator();

} // namespace Inverter
