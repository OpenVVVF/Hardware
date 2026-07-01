#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
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
static constexpr float OFFSET_ROTATE_REVS = 2.0f;
static constexpr float OFFSET_ACQUIRE_START_DEG = 45.0f;
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

void EncoderOffsetCalibrator::resetRampState() {
    m_applied_mod = 0.0f;
    m_ramp_paused = false;
    m_ramp_pause_start_ms = 0;
}

float EncoderOffsetCalibrator::computeRampedMod(uint32_t now_ms) const {
    if (m_ramp_duration_ms == 0U) {
        return m_ramp_target_mod;
    }
    const uint32_t elapsed = now_ms - m_ramp_start_ms;
    if (elapsed >= m_ramp_duration_ms) {
        return m_ramp_target_mod;
    }
    return m_ramp_target_mod * static_cast<float>(elapsed) /
           static_cast<float>(m_ramp_duration_ms);
}

float EncoderOffsetCalibrator::updateCurrentLimitedSPWM(float frequency_hz, uint32_t now_ms) {
    float desired_mod = computeRampedMod(now_ms);
    const float current_limit = openLoopController().rampCurrentLimit();
    const float resume_threshold = 0.8f * current_limit;
    const float i_max = maxPhaseCurrentMagnitude();

    const bool trying_to_increase = (desired_mod > m_applied_mod);
    if (i_max > current_limit && trying_to_increase) {
        if (!m_ramp_paused) {
            m_ramp_paused = true;
            m_ramp_pause_start_ms = now_ms;
            Telemetry::printf("[CAL ENC DBG] ramp paused: I=%.1f A limit=%.1f A",
                              static_cast<double>(i_max),
                              static_cast<double>(current_limit));
        }
        desired_mod = m_applied_mod;
    } else if (m_ramp_paused && i_max <= resume_threshold) {
        m_applied_mod = desired_mod;
        m_ramp_paused = false;
        Telemetry::printf("[CAL ENC DBG] ramp resumed: I=%.1f A limit=%.1f A",
                          static_cast<double>(i_max),
                          static_cast<double>(current_limit));
    } else if (!m_ramp_paused) {
        m_applied_mod = desired_mod;
    }

    if (m_ramp_paused && (now_ms - m_ramp_pause_start_ms) > RAMP_PAUSE_TIMEOUT_MS) {
        PWM_StopSPWM();
        fail("[CAL ENC] FAIL: current limit hit for too long during ramp (I=%.1f A, limit=%.1f A)",
             static_cast<double>(i_max), static_cast<double>(current_limit));
        return 0.0f;
    }

    PWM_SetSPWMParams(frequency_hz, desired_mod);
    return desired_mod;
}

bool EncoderOffsetCalibrator::start(float pole_count, float encoder_cycles_per_rev, float breakaway_mod) {
    if (isActive()) {
        Telemetry::printf("[CAL ENC] already active");
        return false;
    }

    if (pole_count <= 0.0f || encoder_cycles_per_rev <= 0.0f) {
        Telemetry::printf("[CAL ENC] ERROR: invalid pole_count/encoder_cycles");
        return false;
    }

    m_poles = pole_count;
    m_pole_pairs = pole_count / 2.0f;
    m_encoder_cycles_per_rev = encoder_cycles_per_rev;
    m_mech_deg_per_motor_elec_cycle = 360.0f / m_pole_pairs;
    m_breakaway_mod = breakaway_mod;

    if (m_pole_pairs <= 0.0f) {
        Telemetry::printf("[CAL ENC] ERROR: pole_pairs must be > 0");
        return false;
    }

    /* Make sure the open-loop controller is initialized and the timer is
     * running, but do not enable the gate driver yet. */
    if (!openLoopController().isInitialized()) {
        openLoopController().init();
    }

    /* Reset encoder dynamic bounds so the calibration starts from a known
     * state.  The warmup must then rotate the shaft through at least one
     * full mechanical revolution for the bounds to be trustworthy.
     *
     * Keep the encoder angle absolute (referenced to the encoder's own zero)
     * rather than resetting it to zero.  The offset we want is
     * encoder_absolute - field_absolute; if we zero the encoder here we
     * subtract the rotor's arbitrary starting position and the result becomes
     * random. */
    encoderADC().resetBounds();
    m_unwrapped_angle = encoderADC().lastAngle();
    m_last_angle = encoderADC().lastAngle();

    /* Kick off a non-blocking hardware startup sequence.  The gate-driver
     * power/ready waits are handled in update() so telemetry keeps flowing. */
    enterState(State::HW_INIT);
    return true;
}

void EncoderOffsetCalibrator::enterState(State state) {
    m_state = state;
    m_state_start_ms = HAL_GetTick();

    Telemetry::printf("[CAL ENC DBG] enter %s", stateName(state));

    switch (state) {
        case State::HW_INIT:
            /* Enable gate-driver power and hold the driver in reset while the
             * supply stabilizes.  The 50 ms wait is performed in update(). */
            GateDriver_EnablePower(true);
            HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin,
                              GPIO_PIN_RESET);
            break;

        case State::WAIT_READY:
            /* Release gate-driver reset and wait for /RDY in update(). */
            HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin,
                              GPIO_PIN_SET);
            break;

        case State::FIND_VOLTAGE:
            PWM_ResetSPWMElectricalCycles();
            m_last_ramp_ms = HAL_GetTick();
            m_cycles_at_last_move = 0.0f;
            resetRampState();
            PWM_StartSPWM(OFFSET_ROTATION_FREQUENCY_HZ, 0.0f);
            break;

        case State::WARMUP: {
            m_warmup_target_mod = (m_mod > WARMUP_MAX_MOD) ? WARMUP_MAX_MOD : m_mod;
            m_warmup_start_angle = m_unwrapped_angle;
            m_ramp_target_mod = m_warmup_target_mod;
            m_ramp_start_ms = HAL_GetTick();
            m_ramp_duration_ms = 1000U;
            resetRampState();
            PWM_ResetSPWMElectricalCycles();
            PWM_StartSPWM(WARMUP_FREQUENCY_HZ, 0.0f);
            break;
        }

        case State::OFFSET_ROTATE:
            PWM_ResetSPWMElectricalCycles();
            m_rotate_start_angle = m_unwrapped_angle / m_encoder_cycles_per_rev;
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
            m_ramp_target_mod = m_mod;
            m_ramp_start_ms = HAL_GetTick();
            m_ramp_duration_ms = 500U;
            resetRampState();
            PWM_StartSPWM(OFFSET_ROTATION_FREQUENCY_HZ, 0.0f);
            break;

        case State::DONE:
        case State::FAIL:
        default:
            break;
    }
}

void EncoderOffsetCalibrator::restoreHardware() {
    PWM_StopSPWM();
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
    GateDriver_DisableOutputs();
}

void EncoderOffsetCalibrator::fail(const char* reason_fmt, ...) {
    va_list args;
    va_start(args, reason_fmt);
    Telemetry::vprintf(reason_fmt, args);
    va_end(args);

    restoreHardware();
    m_state = State::FAIL;
}

void EncoderOffsetCalibrator::sampleEncoderAngle() {
    uint16_t raw_sin = 0U;
    uint16_t raw_cos = 0U;
    float angle = 0.0f;
    if (!encoderADC().sample(angle, raw_sin, raw_cos)) {
        return;
    }

    float delta = angle - m_last_angle;
    if (delta > 180.0f) {
        delta -= 360.0f;
    } else if (delta < -180.0f) {
        delta += 360.0f;
    }
    m_unwrapped_angle += delta;
    m_last_angle = angle;
    m_last_raw_sin = raw_sin;
    m_last_raw_cos = raw_cos;
}

float EncoderOffsetCalibrator::encoderMechanicalAngle() const {
    return m_unwrapped_angle / m_encoder_cycles_per_rev;
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

void EncoderOffsetCalibrator::accumulateOffsetSample() {
    const float encoder_mech_deg = encoderMechanicalAngle();
    const float field_mech_deg = fieldMechanicalAngle();

    /* The physical offset is the encoder angle when the stator field points
     * at U-high.  If the encoder counts in the same direction as the field,
     * that is encoder - field; if it counts opposite, it is encoder + field.
     * The sign is determined from the first few degrees of rotation. */
    float offset = (m_sign < 0)
                       ? (encoder_mech_deg + field_mech_deg)
                       : (encoder_mech_deg - field_mech_deg);

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

    sampleEncoderAngle();
    const uint32_t now_ms = HAL_GetTick();

    switch (m_state) {
        case State::HW_INIT: {
            /* Wait for gate-driver power to stabilize, then release reset and
             * wait for /RDY. */
            if ((now_ms - m_state_start_ms) >= 50U) {
                enterState(State::WAIT_READY);
            }
            break;
        }

        case State::WAIT_READY: {
            bool ready = GateDriver_IsReady();
            bool fault = GateDriver_IsFault();
            if (ready && !fault) {
                PWM_ClearFault();
                PWM_StartPhase(0);
                PWM_StartPhase(1);
                PWM_StartPhase(2);

                Telemetry::printf("[CAL ENC] started for %.0f poles (%.0f pole pairs), encoder_cycles=%.2f",
                                  static_cast<double>(m_poles),
                                  static_cast<double>(m_pole_pairs),
                                  static_cast<double>(m_encoder_cycles_per_rev));

                if (m_breakaway_mod > 0.0f) {
                    m_mod = m_breakaway_mod * TORQUE_MARGIN;
                    if (m_mod > CAL_MAX_MOD) {
                        m_mod = CAL_MAX_MOD;
                    }
                    Telemetry::printf("[CAL ENC] using provided breakaway mod %.3f -> rotate mod %.3f",
                                      static_cast<double>(m_breakaway_mod),
                                      static_cast<double>(m_mod));
                    enterState(State::WARMUP);
                } else {
                    m_mod = 0.0f;
                    enterState(State::FIND_VOLTAGE);
                }
            } else if (fault || (now_ms - m_state_start_ms) > 500U) {
                restoreHardware();
                fail("[CAL ENC] ERROR: gate driver not ready or fault latched | ready=%s fault=%s",
                     ready ? "Y" : "N", fault ? "Y" : "N");
            }
            break;
        }

        case State::WARMUP: {
            const float applied_mod = updateCurrentLimitedSPWM(WARMUP_FREQUENCY_HZ, now_ms);
            if (isFailed()) {
                break;
            }

            const float moved_mech =
                std::fabs((m_unwrapped_angle / m_encoder_cycles_per_rev) - m_warmup_start_angle);

            if ((now_ms - m_last_dbg_ms) >= 500U) {
                m_last_dbg_ms = now_ms;
                Telemetry::printf("[CAL ENC DBG] warmup: enc_mech=%.3f mod=%.3f moved=%.1f ms=%lu",
                                  static_cast<double>(m_unwrapped_angle / m_encoder_cycles_per_rev),
                                  static_cast<double>(applied_mod),
                                  static_cast<double>(moved_mech),
                                  static_cast<unsigned long>(now_ms - m_state_start_ms));
            }

            /* Require both time and a full mechanical revolution so the encoder
             * dynamic min/max bounds capture a complete sin/cos cycle. */
            const bool time_done = (now_ms - m_state_start_ms) >= WARMUP_MS;
            const bool rev_done = moved_mech >= 360.0f;
            if (time_done && rev_done) {
                PWM_StopSPWM();
                Telemetry::printf("[CAL ENC DBG] warmup done: moved %.1f deg", static_cast<double>(moved_mech));
                enterState(State::OFFSET_ROTATE);
            } else if (time_done && !rev_done) {
                /* The rotor did not make a full revolution during warmup.  Keep
                 * going a little longer, but do not spin forever. */
                if ((now_ms - m_state_start_ms) > (WARMUP_MS + 5000U)) {
                    PWM_StopSPWM();
                    fail("[CAL ENC] FAIL: warmup did not produce a full revolution (moved %.1f deg)",
                         static_cast<double>(moved_mech));
                }
            }
            break;
        }

        case State::FIND_VOLTAGE: {
            const float angle_cycles = m_unwrapped_angle / 360.0f;
            if (std::fabs(angle_cycles - m_cycles_at_last_move) > NOISE_THRESHOLD_CYCLES) {
                m_cycles_at_last_move = angle_cycles;
                m_last_move_ms = now_ms;
            }

            if ((now_ms - m_last_ramp_ms) >= RAMP_PERIOD_MS) {
                m_last_ramp_ms = now_ms;
                m_mod += RAMP_STEP;
                if (m_mod > CAL_MAX_MOD) {
                    m_mod = CAL_MAX_MOD;
                }

                float desired_mod = m_mod;
                const float current_limit = openLoopController().rampCurrentLimit();
                const float resume_threshold = 0.8f * current_limit;
                const float i_max = maxPhaseCurrentMagnitude();
                const bool trying_to_increase = (desired_mod > m_applied_mod);

                if (i_max > current_limit && trying_to_increase) {
                    if (!m_ramp_paused) {
                        m_ramp_paused = true;
                        m_ramp_pause_start_ms = now_ms;
                        Telemetry::printf("[CAL ENC DBG] ramp paused: I=%.1f A limit=%.1f A",
                                          static_cast<double>(i_max),
                                          static_cast<double>(current_limit));
                    }
                    desired_mod = m_applied_mod;
                    m_mod = desired_mod;
                } else if (m_ramp_paused && i_max <= resume_threshold) {
                    m_applied_mod = desired_mod;
                    m_ramp_paused = false;
                    Telemetry::printf("[CAL ENC DBG] ramp resumed: I=%.1f A limit=%.1f A",
                                      static_cast<double>(i_max),
                                      static_cast<double>(current_limit));
                } else if (!m_ramp_paused) {
                    m_applied_mod = desired_mod;
                }

                if (m_ramp_paused && (now_ms - m_ramp_pause_start_ms) > RAMP_PAUSE_TIMEOUT_MS) {
                    PWM_StopSPWM();
                    fail("[CAL ENC] FAIL: current limit hit for too long during voltage ramp (I=%.1f A, limit=%.1f A)",
                         static_cast<double>(i_max), static_cast<double>(current_limit));
                    break;
                }

                PWM_SetSPWMParams(OFFSET_ROTATION_FREQUENCY_HZ, desired_mod);
                Telemetry::printf("[CAL ENC DBG] ramp mod=%.3f angle_cycles=%.3f",
                                  static_cast<double>(desired_mod),
                                  static_cast<double>(angle_cycles));
            }

            if (m_mod >= RAMP_STEP && std::fabs(angle_cycles) >= BREAKAWAY_DETECT_CYCLES) {
                m_breakaway_mod = m_mod;
                float boosted = m_mod * TORQUE_MARGIN;
                if (boosted > CAL_MAX_MOD) {
                    boosted = CAL_MAX_MOD;
                }
                m_mod = boosted;
                PWM_StopSPWM();
                Telemetry::printf("[CAL ENC] breakaway at mod=%.3f -> rotate mod=%.3f",
                                  static_cast<double>(m_breakaway_mod),
                                  static_cast<double>(m_mod));
                enterState(State::WARMUP);
                return;
            }

            if (m_mod >= CAL_MAX_MOD && (now_ms - m_last_move_ms) > MOVE_TIMEOUT_MS) {
                PWM_StopSPWM();
                fail("[CAL ENC] FAIL: rotor did not move during voltage ramp");
                return;
            }

            if ((now_ms - m_state_start_ms) > FIND_VOLTAGE_TIMEOUT_MS) {
                PWM_StopSPWM();
                fail("[CAL ENC] FAIL: voltage ramp timed out");
                return;
            }
            break;
        }

        case State::OFFSET_ROTATE: {
            const float applied_mod = updateCurrentLimitedSPWM(OFFSET_ROTATION_FREQUENCY_HZ, now_ms);
            if (isFailed()) {
                break;
            }

            const float encoder_mech = m_unwrapped_angle / m_encoder_cycles_per_rev;
            const float moved_mech = std::fabs(encoder_mech - m_rotate_start_angle);
            const bool ramp_done = (applied_mod >= 0.99f * m_ramp_target_mod);

            if ((now_ms - m_last_dbg_ms) >= 500U) {
                m_last_dbg_ms = now_ms;
                Telemetry::printf("[CAL ENC DBG] rotate: enc_mech=%.3f mod=%.3f moved=%.1f samples=%d",
                                  static_cast<double>(encoder_mech),
                                  static_cast<double>(applied_mod),
                                  static_cast<double>(moved_mech),
                                  m_sample_count);
            }

            /* Wait until the rotor has clearly started following the field
             * before including samples.  Use the initial transient region to
             * determine whether the encoder counts with or against the field
             * direction so the offset formula uses the correct sign. */
            if (!m_offset_acquisition_active) {
                const float enc = encoderMechanicalAngle();
                const float fld = fieldMechanicalAngle();

                if (m_sign == 0) {
                    if (m_first_encoder_mech == 0.0f && m_first_field_mech == 0.0f) {
                        m_first_encoder_mech = enc;
                        m_first_field_mech = fld;
                    } else {
                        const float d_enc = enc - m_first_encoder_mech;
                        const float d_fld = fld - m_first_field_mech;
                        if (std::fabs(d_enc) >= 10.0f && std::fabs(d_fld) >= 10.0f) {
                            m_sign = (d_enc * d_fld > 0.0f) ? 1 : -1;
                            Telemetry::printf("[CAL ENC DBG] detected sign=%d (d_enc=%.2f d_fld=%.2f)",
                                              m_sign,
                                              static_cast<double>(d_enc),
                                              static_cast<double>(d_fld));
                        }
                    }
                }

                if (ramp_done && moved_mech >= OFFSET_ACQUIRE_START_DEG && m_sign != 0) {
                    m_offset_acquisition_active = true;
                    m_last_encoder_mech = enc;
                    m_last_field_mech = fld;
                    Telemetry::printf("[CAL ENC DBG] acquisition started");
                }
            } else {
                if ((now_ms - m_last_offset_sample_ms) >= OFFSET_SAMPLE_PERIOD_MS) {
                    m_last_offset_sample_ms = now_ms;
                    accumulateOffsetSample();
                }
            }

            const float target_mech = OFFSET_ROTATE_REVS * 360.0f;
            if (moved_mech >= target_mech) {
                PWM_StopSPWM();
                if (m_sample_count == 0) {
                    fail("[CAL ENC] FAIL: rotation finished with no samples");
                    return;
                }
                const float avg_offset =
                    wrapOffset(static_cast<float>(m_sum_offset / static_cast<double>(m_sample_count)),
                               m_mech_deg_per_motor_elec_cycle);
                m_average_offset = avg_offset;
                restoreHardware();
                Telemetry::printf("[CAL ENC] DONE: avg=%.3f deg samples=%d revs=%.1f",
                                  static_cast<double>(m_average_offset),
                                  m_sample_count,
                                  static_cast<double>(moved_mech / 360.0f));
                Telemetry::printf("[CAL ENC] bounds: sin=[%u,%u] cos=[%u,%u]",
                                  static_cast<unsigned int>(encoderADC().sinMin()),
                                  static_cast<unsigned int>(encoderADC().sinMax()),
                                  static_cast<unsigned int>(encoderADC().cosMin()),
                                  static_cast<unsigned int>(encoderADC().cosMax()));
                m_state = State::DONE;
                return;
            }

            if ((now_ms - m_state_start_ms) > OFFSET_ROTATE_TIMEOUT_MS) {
                PWM_StopSPWM();
                fail("[CAL ENC] FAIL: rotation timed out (moved %.1f deg)",
                     static_cast<double>(moved_mech));
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
