#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include <cmath>
#include <cstdarg>

namespace Inverter {

static constexpr float CAL_MAX_MOD = 0.50f;
static constexpr float HOLD_MAX_MOD = 0.50f;
static constexpr float WARMUP_MAX_MOD = 0.25f;
static constexpr float RAMP_STEP = 0.01f;
static constexpr float TORQUE_MARGIN = 2.50f;

static constexpr uint32_t WARMUP_MS = 5000U;
static constexpr float WARMUP_FREQUENCY_HZ = 1.0f;
static constexpr float OFFSET_ROTATION_FREQUENCY_HZ = 1.0f;
static constexpr float OFFSET_ROTATE_REVS = 3.0f;
static constexpr float OFFSET_ACQUIRE_START_DEG = 180.0f;
static constexpr uint32_t OFFSET_SAMPLE_PERIOD_MS = 10U;

static constexpr float NOISE_THRESHOLD_CYCLES = 0.02f;
static constexpr float BREAKAWAY_DETECT_CYCLES = 0.15f;
static constexpr uint32_t RAMP_PERIOD_MS = 50U;
static constexpr uint32_t MOVE_TIMEOUT_MS = 3000U;
static constexpr uint32_t FIND_VOLTAGE_TIMEOUT_MS = 15000U;
static constexpr uint32_t OFFSET_ROTATE_TIMEOUT_MS = 60000U;
static constexpr uint32_t RAMP_PAUSE_TIMEOUT_MS = 200U;

static constexpr float VOLTAGE_ANGLE_U_HIGH_RAD = 1.57079632679f;
static constexpr float RAD_TO_DEG = 57.2957795131f;

static EncoderOffsetCalibrator s_instance;

EncoderOffsetCalibrator& EncoderOffsetCalibrator::instance() {
    return s_instance;
}

static const char* stateName(EncoderOffsetCalibrator::State state) {
    switch (state) {
        case EncoderOffsetCalibrator::State::IDLE:          return "IDLE";
        case EncoderOffsetCalibrator::State::HW_INIT:       return "HW_INIT";
        case EncoderOffsetCalibrator::State::WAIT_READY:    return "WAIT_READY";
        case EncoderOffsetCalibrator::State::WARMUP:        return "WARMUP";
        case EncoderOffsetCalibrator::State::FIND_VOLTAGE:  return "FIND_VOLTAGE";
        case EncoderOffsetCalibrator::State::OFFSET_ROTATE: return "OFFSET_ROTATE";
        case EncoderOffsetCalibrator::State::DONE:          return "DONE";
        case EncoderOffsetCalibrator::State::FAIL:          return "FAIL";
    }
    return "?";
}

bool EncoderOffsetCalibrator::start(float pole_count, float encoder_cycles_per_rev,
                                     float breakaway_mod, float max_mod,
                                     float torque_margin) {
    if (isActive()) {
        Telemetry::printf("[CAL] ENC: already active");
        return false;
    }

    if (pole_count <= 0.0f || encoder_cycles_per_rev <= 0.0f) {
        Telemetry::printf("[CAL] ENC: ERROR: invalid pole_count/encoder_cycles");
        return false;
    }

    m_poles = pole_count;
    m_pole_pairs = pole_count / 2.0f;
    m_encoder_cycles_per_rev = encoder_cycles_per_rev;
    m_mech_deg_per_motor_elec_cycle = 360.0f / m_pole_pairs;
    m_breakaway_mod = breakaway_mod;
    m_max_mod = (max_mod > 0.0f) ? max_mod : 0.50f;
    m_torque_margin = (torque_margin > 1.0f) ? torque_margin : 2.50f;

    if (m_pole_pairs <= 0.0f) {
        Telemetry::printf("[CAL] ENC: ERROR: pole_pairs must be > 0");
        return false;
    }

    if (!openLoopController().isInitialized()) {
        openLoopController().init();
    }

    encoderADC().resetBounds();
    encoderADC().learnBounds(true);
    m_tracker.reset();
    m_hw.begin();

    enterState(State::HW_INIT);
    return true;
}

void EncoderOffsetCalibrator::enterState(State state) {
    m_state = state;
    m_state_start_ms = HAL_GetTick();

    Telemetry::printf("[CAL] ENC DBG: enter %s", stateName(state));

    switch (state) {
        case State::HW_INIT:
            /* Hardware startup is driven by CalibrationHardware; nothing else
             * to set up here. */
            break;

        case State::FIND_VOLTAGE:
            PWM_ResetSPWMElectricalCycles();
            m_breakaway.start(RAMP_STEP, RAMP_PERIOD_MS, m_max_mod,
                              BREAKAWAY_DETECT_CYCLES, m_torque_margin, MOVE_TIMEOUT_MS);
            PWM_StartSPWM(OFFSET_ROTATION_FREQUENCY_HZ, 0.0f);
            break;

        case State::WARMUP: {
            m_tracker.reset();
            const float target = (m_mod > WARMUP_MAX_MOD) ? WARMUP_MAX_MOD : m_mod;
            m_ramp.start(0.0f, target, 1000U, openLoopController().rampCurrentLimit());
            PWM_ResetSPWMElectricalCycles();
            PWM_StartSPWM(WARMUP_FREQUENCY_HZ, 0.0f);

            m_warmup_sign = 0;
            m_warmup_enc_start = m_tracker.unwrappedDegrees() / m_encoder_cycles_per_rev;
            m_warmup_fld_start = fieldMechanicalAngle();
            break;
        }

        case State::OFFSET_ROTATE:
            PWM_ResetSPWMElectricalCycles();
            PWM_StartSPWM(OFFSET_ROTATION_FREQUENCY_HZ, 0.0f);
            /* Reset the tracker here so the offset measurement starts from the
             * same instant as the field angle.  This removes any scale/offset
             * error accumulated during the warmup phase. */
            m_tracker.reset();
            m_rotate_start_encoder = 0.0f;
            m_rotate_start_field = fieldMechanicalAngle();
            m_offset_acquisition_active = false;
            m_sign = 0;
            m_first_encoder_mech = 0.0f;
            m_first_field_mech = 0.0f;
            m_last_encoder_mech = 0.0f;
            m_last_field_mech = 0.0f;
            m_sum_offset = 0.0;
            m_sample_count = 0;
            m_last_offset = 0.0f;
            m_last_offset_sample_ms = 0;
            m_ramp.start(0.0f, m_mod, 500U, openLoopController().rampCurrentLimit());
            break;

        case State::DONE:
        case State::FAIL:
        case State::WAIT_READY:
        default:
            break;
    }
}

void EncoderOffsetCalibrator::restoreHardware() {
    CalibrationHardware::shutdown();
}

void EncoderOffsetCalibrator::stop() {
    if (m_state != State::IDLE && m_state != State::DONE && m_state != State::FAIL) {
        PWM_StopSPWM();
        restoreHardware();
        encoderADC().learnBounds(false);
        m_state = State::IDLE;
        Telemetry::printf("[CAL] ENC: stopped by user");
    }
}

void EncoderOffsetCalibrator::fail(const char* reason_fmt, ...) {
    va_list args;
    va_start(args, reason_fmt);
    Telemetry::vprintf(reason_fmt, args);
    va_end(args);

    restoreHardware();
    encoderADC().learnBounds(false);
    m_state = State::FAIL;
}

void EncoderOffsetCalibrator::sample() {
    uint16_t raw_sin = 0U;
    uint16_t raw_cos = 0U;
    float angle = 0.0f;
    if (encoderADC().sample(angle, raw_sin, raw_cos)) {
        m_tracker.update();
        m_last_raw_sin = raw_sin;
        m_last_raw_cos = raw_cos;
    }
}

float EncoderOffsetCalibrator::encoderMechanicalAngle() const {
    return m_tracker.unwrappedDegrees() / m_encoder_cycles_per_rev;
}

float EncoderOffsetCalibrator::fieldMechanicalAngle() const {
    const float spwm_angle = PWM_GetSPWMAngle();
    const uint32_t cycles = PWM_GetSPWMElectricalCycles();

    /* Field mechanical angle relative to the U-high vector. */
    float field_elec_deg = static_cast<float>(cycles) * 360.0f + spwm_angle * RAD_TO_DEG;
    field_elec_deg -= VOLTAGE_ANGLE_U_HIGH_RAD * RAD_TO_DEG;
    return field_elec_deg / m_pole_pairs;
}

float EncoderOffsetCalibrator::wrapOffset(float offset, float period) {
    float wrapped = offset - period * std::floor(offset / period);
    if (wrapped < 0.0f) {
        wrapped += period;
    }
    if (wrapped >= period) {
        wrapped -= period;
    }
    return wrapped;
}

static bool encoderAngleReliable() {
    const auto& enc = encoderADC();
    return enc.boundsValid() &&
           (enc.sinMax() - enc.sinMin() > 10000U) &&
           (enc.cosMax() - enc.cosMin() > 10000U);
}

void EncoderOffsetCalibrator::accumulateOffsetSample() {
    if (!encoderAngleReliable()) {
        return;
    }

    const float encoder_raw_deg = encoderADC().lastAngle();
    const float field_mech_deg = fieldMechanicalAngle();

    /* The physical offset is the encoder angle when the stator field points
     * at U-high.  Use the raw absolute encoder angle (0..360) and unwrap it
     * against the known continuous field angle; this avoids any drift from
     * the movement tracker starting at an arbitrary field angle. */
    float offset = (m_sign < 0)
                       ? (encoder_raw_deg + field_mech_deg)
                       : (encoder_raw_deg - field_mech_deg);

    /* The offset is only unique modulo (360° / pole_pairs) because the
     * motor has multiple pole pairs.  Keep successive samples in the same
     * branch so a boundary does not corrupt the average. */
    if (m_sample_count > 0) {
        const float half_period = 0.5f * m_mech_deg_per_motor_elec_cycle;
        while ((offset - m_last_offset) > half_period) {
            offset -= m_mech_deg_per_motor_elec_cycle;
        }
        while ((m_last_offset - offset) > half_period) {
            offset += m_mech_deg_per_motor_elec_cycle;
        }
    }

    m_sum_offset += static_cast<double>(offset);
    m_last_offset = offset;
    ++m_sample_count;
}

void EncoderOffsetCalibrator::update() {
    if (m_state == State::IDLE || m_state == State::DONE || m_state == State::FAIL) {
        return;
    }

    sample();
    const uint32_t now_ms = HAL_GetTick();

    switch (m_state) {
        case State::HW_INIT: {
            m_hw.update();
            if (m_hw.hasFailed()) {
                fail("[CAL] ENC: ERROR: gate driver not ready or fault latched");
            } else if (m_hw.isReady()) {
                PWM_ClearFault();
                PWM_StartPhase(0);
                PWM_StartPhase(1);
                PWM_StartPhase(2);

                Telemetry::printf("[CAL] ENC: started for %.0f poles (%.0f pole pairs), encoder_cycles=%.2f",
                                  static_cast<double>(m_poles),
                                  static_cast<double>(m_pole_pairs),
                                  static_cast<double>(m_encoder_cycles_per_rev));

                if (m_breakaway_mod > 0.0f) {
                    m_mod = m_breakaway_mod * m_torque_margin;
                    if (m_mod > m_max_mod) {
                        m_mod = m_max_mod;
                    }
                    Telemetry::printf("[CAL] ENC: using provided breakaway mod %.3f -> rotate mod %.3f",
                                      static_cast<double>(m_breakaway_mod),
                                      static_cast<double>(m_mod));
                    enterState(State::WARMUP);
                } else {
                    m_mod = 0.0f;
                    enterState(State::FIND_VOLTAGE);
                }
            }
            break;
        }

        case State::WARMUP: {
            const auto ramp_status = m_ramp.update(now_ms);
            if (ramp_status == CurrentLimitedRamp::Status::ABORTED) {
                fail("[CAL] ENC: FAIL: warmup current limit hit");
                break;
            }

            PWM_SetSPWMParams(WARMUP_FREQUENCY_HZ, m_ramp.applied());

            const float enc_mech = m_tracker.unwrappedDegrees() / m_encoder_cycles_per_rev;
            const float moved_enc = std::fabs(enc_mech - m_warmup_enc_start);
            const float moved_field = std::fabs(fieldMechanicalAngle() - m_warmup_fld_start);

            if ((now_ms - m_last_dbg_ms) >= 500U) {
                m_last_dbg_ms = now_ms;
                Telemetry::printf("[CAL] ENC DBG: warmup: enc_mech=%.3f mod=%.3f enc_moved=%.1f fld_moved=%.1f ms=%lu",
                                  static_cast<double>(enc_mech),
                                  static_cast<double>(m_ramp.applied()),
                                  static_cast<double>(moved_enc),
                                  static_cast<double>(moved_field),
                                  static_cast<unsigned long>(now_ms - m_state_start_ms));
            }

            const bool time_done = (now_ms - m_state_start_ms) >= WARMUP_MS;
            const bool rev_done = moved_field >= 360.0f;
            if (time_done && rev_done) {
                /* Capture final field angle before stopping SPWM, because
                 * PWM_GetSPWMAngle() returns 0 while SPWM is stopped. */
                const float warmup_enc_end = m_tracker.unwrappedDegrees() / m_encoder_cycles_per_rev;
                const float warmup_fld_end = fieldMechanicalAngle();

                PWM_StopSPWM();
                Telemetry::printf("[CAL] ENC DBG: warmup done: enc_moved=%.1f fld_moved=%.1f deg",
                                  static_cast<double>(moved_enc),
                                  static_cast<double>(moved_field));

                /* Determine the encoder/field direction from the whole warmup
                 * revolution; this is much more robust than the first few
                 * degrees after the PWM restart in OFFSET_ROTATE. */
                const float d_enc = warmup_enc_end - m_warmup_enc_start;
                const float d_fld = warmup_fld_end - m_warmup_fld_start;
                if (std::fabs(d_enc) >= 180.0f && std::fabs(d_fld) >= 180.0f) {
                    m_warmup_sign = (d_enc * d_fld > 0.0f) ? 1 : -1;
                    Telemetry::printf("[CAL] ENC DBG: warmup direction d_enc=%.1f d_fld=%.1f sign=%d",
                                      static_cast<double>(d_enc),
                                      static_cast<double>(d_fld),
                                      m_warmup_sign);
                }

                enterState(State::OFFSET_ROTATE);
            } else if (time_done && !rev_done) {
                if ((now_ms - m_state_start_ms) > (WARMUP_MS + 5000U)) {
                    PWM_StopSPWM();
                    fail("[CAL] ENC: FAIL: warmup did not produce a full revolution (enc_moved %.1f fld_moved %.1f deg)",
                         static_cast<double>(moved_enc),
                         static_cast<double>(moved_field));
                }
            }
            break;
        }

        case State::FIND_VOLTAGE: {
            const float angle_cycles = m_tracker.mechanicalCycles();
            float mod = 0.0f;
            const auto status = m_breakaway.update(now_ms, std::fabs(angle_cycles), mod);

            if (status == BreakawayFinder::Status::RUNNING) {
                PWM_SetSPWMParams(OFFSET_ROTATION_FREQUENCY_HZ, mod);
            } else if (status == BreakawayFinder::Status::TIMEOUT) {
                PWM_StopSPWM();
                fail("[CAL] ENC: FAIL: rotor did not move during voltage ramp");
                return;
            } else {
                m_breakaway_mod = m_breakaway.breakawayMod();
                m_mod = mod;  /* already boosted by BreakawayFinder */
                PWM_StopSPWM();
                Telemetry::printf("[CAL] ENC: breakaway at mod=%.3f -> rotate mod=%.3f",
                                  static_cast<double>(m_breakaway_mod),
                                  static_cast<double>(m_mod));
                enterState(State::WARMUP);
                return;
            }

            if ((now_ms - m_state_start_ms) > FIND_VOLTAGE_TIMEOUT_MS) {
                PWM_StopSPWM();
                fail("[CAL] ENC: FAIL: voltage ramp timed out");
                return;
            }
            break;
        }

        case State::OFFSET_ROTATE: {
            const auto ramp_status = m_ramp.update(now_ms);
            if (ramp_status == CurrentLimitedRamp::Status::ABORTED) {
                fail("[CAL] ENC: FAIL: rotation current limit hit");
                break;
            }

            PWM_SetSPWMParams(OFFSET_ROTATION_FREQUENCY_HZ, m_ramp.applied());

            const float field_mech = fieldMechanicalAngle();
            const float moved_field = std::fabs(field_mech - m_rotate_start_field);
            const float encoder_mech = m_tracker.unwrappedDegrees() / m_encoder_cycles_per_rev;
            const bool ramp_done = (ramp_status == CurrentLimitedRamp::Status::DONE);

            if ((now_ms - m_last_dbg_ms) >= 500U) {
                m_last_dbg_ms = now_ms;
                Telemetry::printf("[CAL] ENC DBG: rotate: enc_mech=%.3f fld_mech=%.3f moved=%.1f samples=%d",
                                  static_cast<double>(encoder_mech),
                                  static_cast<double>(field_mech),
                                  static_cast<double>(moved_field),
                                  m_sample_count);
            }

            if (!m_offset_acquisition_active) {
                if (m_sign == 0) {
                    /* Use the robust warmup direction if available; otherwise
                     * fall back to observing a larger initial motion in this
                     * state so noise/transients do not flip the sign. */
                    if (m_warmup_sign != 0) {
                        m_sign = m_warmup_sign;
                        Telemetry::printf("[CAL] ENC DBG: using warmup sign=%d", m_sign);
                    } else if (m_first_encoder_mech == 0.0f && m_first_field_mech == 0.0f) {
                        m_first_encoder_mech = encoder_mech;
                        m_first_field_mech = field_mech;
                    } else {
                        const float d_enc = encoder_mech - m_first_encoder_mech;
                        const float d_fld = field_mech - m_first_field_mech;
                        if (std::fabs(d_enc) >= 45.0f && std::fabs(d_fld) >= 45.0f) {
                            m_sign = (d_enc * d_fld > 0.0f) ? 1 : -1;
                            Telemetry::printf("[CAL] ENC DBG: detected sign=%d (d_enc=%.2f d_fld=%.2f)",
                                              m_sign,
                                              static_cast<double>(d_enc),
                                              static_cast<double>(d_fld));
                        }
                    }
                }

                if (ramp_done && moved_field >= OFFSET_ACQUIRE_START_DEG && m_sign != 0) {
                    m_offset_acquisition_active = true;
                    m_last_encoder_mech = encoder_mech;
                    m_last_field_mech = field_mech;
                    Telemetry::printf("[CAL] ENC DBG: acquisition started");
                }
            } else {
                if ((now_ms - m_last_offset_sample_ms) >= OFFSET_SAMPLE_PERIOD_MS) {
                    m_last_offset_sample_ms = now_ms;
                    accumulateOffsetSample();
                }
            }

            const float target_mech = OFFSET_ROTATE_REVS * 360.0f;
            if (moved_field >= target_mech) {
                PWM_StopSPWM();
                if (m_sample_count == 0) {
                    fail("[CAL] ENC: FAIL: rotation finished with no samples");
                    return;
                }
                const float avg_offset =
                    wrapOffset(static_cast<float>(m_sum_offset / static_cast<double>(m_sample_count)),
                               m_mech_deg_per_motor_elec_cycle);
                m_average_offset = avg_offset;

                /* Measure the true encoder cycles per mechanical revolution from
                 * the raw unwrapped encoder angle over the known 3-rev rotation.
                 * This avoids the zero-crossing counting errors that can happen
                 * during the rough pole-cal rotation. */
                const float raw_unwrapped_deg = m_tracker.unwrappedDegrees();
                m_measured_encoder_cycles_per_rev =
                    std::fabs(raw_unwrapped_deg) / (360.0f * OFFSET_ROTATE_REVS);

                restoreHardware();
                encoderADC().learnBounds(false);
                Telemetry::printf("[CAL] ENC: DONE: avg=%.3f deg samples=%d revs=%.1f",
                                  static_cast<double>(m_average_offset),
                                  m_sample_count,
                                  static_cast<double>(moved_field / 360.0f));
                Telemetry::printf("[CAL] ENC: bounds: sin=[%u,%u] cos=[%u,%u]",
                                  static_cast<unsigned int>(encoderADC().sinMin()),
                                  static_cast<unsigned int>(encoderADC().sinMax()),
                                  static_cast<unsigned int>(encoderADC().cosMin()),
                                  static_cast<unsigned int>(encoderADC().cosMax()));
                m_state = State::DONE;
                return;
            }

            if ((now_ms - m_state_start_ms) > OFFSET_ROTATE_TIMEOUT_MS) {
                PWM_StopSPWM();
                fail("[CAL] ENC: FAIL: rotation timed out (moved %.1f deg)",
                     static_cast<double>(moved_field));
                return;
            }
            break;
        }

        default:
            break;
    }
}

EncoderOffsetCalibrator& encoderOffsetCalibrator() {
    return EncoderOffsetCalibrator::instance();
}

} // namespace Inverter
