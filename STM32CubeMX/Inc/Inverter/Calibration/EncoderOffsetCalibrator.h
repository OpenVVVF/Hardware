#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Automated encoder offset calibration.
 *
 * Uses the motor pole count (from calpoles / PoleEstimator) to step the rotor
 * through every pole-pair alignment position over two mechanical revolutions,
 * measuring the encoder mechanical angle at each stop.  The average difference
 * between the measured mechanical angle and the expected electrical angle is
 * the encoder offset.
 *
 * The calibration first auto-detects a safe holding voltage by ramping
 * modulation until the rotor breaks away, then reuses that voltage for all
 * alignment steps.
 */
class EncoderOffsetCalibrator {
public:
    EncoderOffsetCalibrator() = default;

    /**
     * @brief Start encoder offset calibration.
     *
     * The motor must be stopped.  The pole count is the total number of rotor
     * poles (e.g. 10 for a 10-pole / 5-pole-pair motor).  encoder_cycles_per_rev
     * is the number of electrical cycles the encoder produces per mechanical
     * revolution (measure with encodercal).
     *
     * @param pole_count            Total motor pole count.
     * @param encoder_cycles_per_rev Encoder electrical cycles per mechanical rev.
     * @param breakaway_mod         Optional pre-determined breakaway modulation.
     *                              If 0, the calibrator will auto-ramp to find a
     *                              safe voltage.
     * @return true if the gate driver could be enabled and PWM is ready.
     */
    bool start(float pole_count, float encoder_cycles_per_rev, float breakaway_mod = 0.0f);

    /**
     * @brief Non-blocking state-machine update.  Call at ~100 Hz from the main
     * loop.
     */
    void update();

    /**
     * @brief True while a calibration is in progress.
     */
    bool isActive() const {
        return m_state != State::IDLE && m_state != State::DONE && m_state != State::FAIL;
    }

    /**
     * @brief True if the last calibration finished successfully.
     */
    bool isDone() const { return m_state == State::DONE; }

    /**
     * @brief True if the last calibration failed.
     */
    bool isFailed() const { return m_state == State::FAIL; }

    /**
     * @brief Average encoder offset in mechanical degrees after a successful run.
     *
     * The offset is the mechanical encoder angle that corresponds to electrical
     * angle 0 deg (d-axis aligned with phase U).  It is wrapped into
     * [0, 360 / pole_pairs).
     */
    float averageOffset() const { return m_average_offset; }

    /**
     * @brief Number of successful offset samples collected so far.
     */
    int sampleCount() const { return m_sample_count; }

    static EncoderOffsetCalibrator& instance();

    enum class State {
        IDLE,
        FIND_VOLTAGE,
        SETTLE,
        HOLD,
        ROTATE,
        DONE,
        FAIL
    };

private:
    void enterState(State state);
    void fail(const char* reason_fmt, ...);
    void restoreHardware();

    void sampleEncoderAngle();
    bool isEncoderSettled() const;
    void holdCurrentAngle();
    void measureAndLog();
    static float wrapOffset(float offset, float period);

    State    m_state = State::IDLE;
    float    m_poles = 0.0f;
    float    m_pole_pairs = 0.0f;
    float    m_encoder_cycles_per_rev = 1.0f;
    float    m_mech_deg_per_motor_elec_cycle = 0.0f;
    float    m_enc_deg_per_motor_elec_cycle = 0.0f;
    float    m_mod = 0.0f;
    float    m_breakaway_mod = 0.0f;

    int      m_step = 0;
    int      m_total_steps = 0;

    /* Encoder tracking. */
    float    m_last_angle = 0.0f;
    float    m_unwrapped_angle = 0.0f;
    float    m_settle_angle = 0.0f;
    uint32_t m_settle_start_ms = 0;

    /* Timing. */
    uint32_t m_state_start_ms = 0;
    uint32_t m_last_ramp_ms = 0;
    uint32_t m_last_move_ms = 0;
    uint32_t m_last_dbg_ms = 0;
    float    m_cycles_at_last_move = 0.0f;

    /* Rotation tracking. */
    uint32_t m_elec_cycles_start = 0;
    float    m_rotate_start_angle = 0.0f;

    /* Results. */
    float    m_sum_offset = 0.0f;
    int      m_sample_count = 0;
    float    m_average_offset = 0.0f;

    static constexpr int MAX_STEPS = 32;
    float    m_offsets[MAX_STEPS] = {};
};

EncoderOffsetCalibrator& encoderOffsetCalibrator();

} // namespace Inverter
