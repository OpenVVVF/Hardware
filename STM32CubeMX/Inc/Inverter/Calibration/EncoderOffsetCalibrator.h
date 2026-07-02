#pragma once

#include "Inverter/Calibration/Common/CalibrationHardware.h"
#include "Inverter/Calibration/Common/EncoderTracker.h"
#include "Inverter/Calibration/Common/CurrentLimitedRamp.h"
#include "Inverter/Calibration/Common/BreakawayFinder.h"

#include <cstdint>

namespace Inverter {

/**
 * @brief Automated encoder offset calibration.
 *
 * Rotates the stator field slowly through one or more full mechanical
 * revolutions and averages the difference between the encoder mechanical
 * angle and the commanded field mechanical angle.  Averaging over whole
 * revolutions makes the result robust against cogging and pole-pair
 * ambiguity.
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
     * @param max_mod               Hard cap on modulation index during warmup/rotation.
     * @param torque_margin         Multiplier applied to breakaway_mod to choose
     *                              the rotation modulation.
     * @return true if the gate driver could be enabled and PWM is ready.
     */
    bool start(float pole_count, float encoder_cycles_per_rev, float breakaway_mod = 0.0f,
               float max_mod = 0.50f, float torque_margin = 2.50f);

    /**
     * @brief Non-blocking state-machine update.  Call at ~100 Hz from the main
     * loop.
     */
    void update();

    /**
     * @brief Abort a running calibration and turn off all switching.
     */
    void stop();

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
     * The offset is the mechanical encoder angle that corresponds to the
     * U-high electrical vector.  For a motor with N pole pairs this is only
     * unique modulo 360/N mechanical degrees, so the result is wrapped into
     * [0, 360 / pole_pairs).
     */
    float averageOffset() const { return m_average_offset; }

    /**
     * @brief Number of successful offset samples collected during the last run.
     */
    int sampleCount() const { return m_sample_count; }

    /**
     * @brief Encoder electrical cycles per mechanical revolution measured during
     * the last offset rotation.
     *
     * This is computed from the raw unwrapped encoder angle over the known 3-rev
     * field rotation, so it is independent of the encoder-cycle zero-crossing
     * counter used during pole calibration.
     */
    float measuredEncoderCyclesPerRev() const { return m_measured_encoder_cycles_per_rev; }

    static EncoderOffsetCalibrator& instance();

    enum class State {
        IDLE,
        HW_INIT,
        WAIT_READY,
        WARMUP,
        FIND_VOLTAGE,
        OFFSET_ROTATE,
        DONE,
        FAIL
    };

private:
    void enterState(State state);
    void fail(const char* reason_fmt, ...);
    void restoreHardware();

    void sample();
    void accumulateOffsetSample();
    float encoderMechanicalAngle() const;
    float fieldMechanicalAngle() const;
    static float wrapOffset(float offset, float period);
    State    m_state = State::IDLE;
    float    m_poles = 0.0f;
    float    m_pole_pairs = 0.0f;
    float    m_encoder_cycles_per_rev = 1.0f;
    float    m_mech_deg_per_motor_elec_cycle = 0.0f;
    float    m_mod = 0.0f;
    float    m_breakaway_mod = 0.0f;
    float    m_max_mod = 0.50f;
    float    m_torque_margin = 2.50f;

    CalibrationHardware m_hw;
    EncoderTracker      m_tracker;
    CurrentLimitedRamp  m_ramp;
    BreakawayFinder     m_breakaway;

    uint16_t m_last_raw_sin = 0U;
    uint16_t m_last_raw_cos = 0U;

    /* Timing. */
    uint32_t m_state_start_ms = 0;
    uint32_t m_last_dbg_ms = 0;
    uint32_t m_last_offset_sample_ms = 0;

    /* Rotation tracking. */
    float    m_rotate_start_encoder = 0.0f;
    float    m_rotate_start_field = 0.0f;
    bool     m_offset_acquisition_active = false;

    /* Encoder/field direction detection.
     * +1: encoder and field rotate in the same direction.
     * -1: encoder and field rotate in opposite directions.
     *  0: not determined yet (acquisition region). */
    int      m_sign = 0;
    float    m_first_encoder_mech = 0.0f;
    float    m_first_field_mech = 0.0f;
    float    m_last_encoder_mech = 0.0f;
    float    m_last_field_mech = 0.0f;

    /* Direction measured over the full warmup revolution. */
    int      m_warmup_sign = 0;
    float    m_warmup_enc_start = 0.0f;
    float    m_warmup_fld_start = 0.0f;

    /* Results. */
    double   m_sum_offset = 0.0;
    int      m_sample_count = 0;
    float    m_last_offset = 0.0f;
    float    m_average_offset = 0.0f;
    float    m_measured_encoder_cycles_per_rev = 0.0f;
};

EncoderOffsetCalibrator& encoderOffsetCalibrator();

} // namespace Inverter
