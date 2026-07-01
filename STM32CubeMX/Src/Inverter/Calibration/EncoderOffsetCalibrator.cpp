#include "Inverter/Calibration/EncoderOffsetCalibrator.h"

#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "tim.h"

#include <cstdarg>
#include <cmath>

namespace Inverter {

namespace {

const char* stateName(EncoderOffsetCalibrator::State state) {
    switch (state) {
        case EncoderOffsetCalibrator::State::IDLE:          return "IDLE";
        case EncoderOffsetCalibrator::State::FIND_VOLTAGE:  return "FIND_VOLTAGE";
        case EncoderOffsetCalibrator::State::SETTLE:        return "SETTLE";
        case EncoderOffsetCalibrator::State::HOLD:          return "HOLD";
        case EncoderOffsetCalibrator::State::ROTATE:        return "ROTATE";
        case EncoderOffsetCalibrator::State::DONE:          return "DONE";
        case EncoderOffsetCalibrator::State::FAIL:          return "FAIL";
    }
    return "?";
}

} // anonymous namespace

/* Angle in radians where phase U is driven high (SVPWM sin reference). */
static constexpr float VOLTAGE_ANGLE_U_HIGH_RAD = 1.57079632679f; /* pi/2 */

static constexpr float CAL_MAX_MOD = 0.50f;
static constexpr float RAMP_STEP = 0.01f;
static constexpr uint32_t RAMP_PERIOD_MS = 50U;
static constexpr float BREAKAWAY_DETECT_CYCLES = 0.15f;
static constexpr float TORQUE_MARGIN = 2.50f;

static constexpr uint32_t SETTLE_TIMEOUT_MS = 8000U;
static constexpr uint32_t MOVE_TIMEOUT_MS = 8000U;
static constexpr uint32_t ROTATE_TIMEOUT_MS = 15000U;
static constexpr uint32_t FIND_VOLTAGE_TIMEOUT_MS = 20000U;
static constexpr float SETTLE_THRESHOLD_CYCLES = 0.05f;
static constexpr float NOISE_THRESHOLD_CYCLES = 0.05f;
static constexpr uint32_t SETTLE_STABLE_MS = 1000U;
static constexpr uint32_t HOLD_SETTLE_MS = 1000U;
static constexpr float ROTATION_FREQUENCY_HZ = 0.5f;
static constexpr float ANGLE_STOP_TOLERANCE_RAD = 0.35f; /* ~20 deg */

static EncoderOffsetCalibrator s_instance;

EncoderOffsetCalibrator& EncoderOffsetCalibrator::instance() {
    return s_instance;
}

EncoderOffsetCalibrator& encoderOffsetCalibrator() {
    return s_instance;
}

bool EncoderOffsetCalibrator::start(float pole_count, float encoder_cycles_per_rev, float breakaway_mod) {
    if (openLoopController().isRunning()) {
        Telemetry::printf("[CAL ENC] stop the motor before starting encoder offset calibration");
        return false;
    }

    if (pole_count < 2.0f || std::fmod(pole_count, 2.0f) != 0.0f) {
        Telemetry::printf("[CAL ENC] ERROR: invalid pole count %.1f", static_cast<double>(pole_count));
        return false;
    }

    if (encoder_cycles_per_rev < 1.0f) {
        Telemetry::printf("[CAL ENC] ERROR: invalid encoder cycles per rev %.3f (must be >= 1)",
                          static_cast<double>(encoder_cycles_per_rev));
        return false;
    }

    m_poles = pole_count;
    m_pole_pairs = m_poles / 2.0f;
    m_encoder_cycles_per_rev = encoder_cycles_per_rev;
    m_mech_deg_per_motor_elec_cycle = 360.0f / m_pole_pairs;
    m_enc_deg_per_motor_elec_cycle = m_mech_deg_per_motor_elec_cycle * m_encoder_cycles_per_rev;

    m_step = 0;
    /* 2 mechanical cycles, one sample per pole-pair alignment position. */
    m_total_steps = static_cast<int>(m_poles);
    if (m_total_steps > MAX_STEPS) {
        m_total_steps = MAX_STEPS;
    }

    m_mod = 0.0f;
    m_breakaway_mod = breakaway_mod;

    m_last_angle = encoderADC().lastAngle();
    m_unwrapped_angle = 0.0f;
    m_settle_angle = 0.0f;
    m_settle_start_ms = 0;

    m_state_start_ms = 0;
    m_last_ramp_ms = 0;
    m_last_move_ms = HAL_GetTick();
    m_last_dbg_ms = 0;
    m_cycles_at_last_move = 0.0f;
    m_elec_cycles_start = 0;
    m_rotate_start_angle = 0.0f;

    m_sum_offset = 0.0f;
    m_sample_count = 0;
    m_average_offset = 0.0f;
    for (int i = 0; i < MAX_STEPS; ++i) {
        m_offsets[i] = 0.0f;
    }

    /* Park outputs and enable gate driver. */
    PWM_StopSPWM();
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
    PWM_ClearFault();
    GateDriver_EnableOutputs();

    /* Ensure all TIM1 phase channels and the ADC trigger channel are enabled. */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIM_OC_Start(&htim1, TIM_CHANNEL_4);

    bool ready = false;
    bool fault = true;
    for (int i = 0; i < 100; ++i) {
        ready = GateDriver_IsReady();
        fault = GateDriver_IsFault();
        if (ready && !fault) break;
        HAL_Delay(5);
    }

    if (!ready || fault) {
        restoreHardware();
        Telemetry::printf("[CAL ENC] ERROR: gate driver not ready or fault latched");
        return false;
    }

    Telemetry::printf("[CAL ENC] started for %.0f poles (%.0f pole pairs), encoder_cycles=%.2f, %d samples",
                      static_cast<double>(m_poles),
                      static_cast<double>(m_pole_pairs),
                      static_cast<double>(m_encoder_cycles_per_rev),
                      m_total_steps);

    if (m_breakaway_mod > 0.0f) {
        m_mod = m_breakaway_mod * TORQUE_MARGIN;
        if (m_mod > CAL_MAX_MOD) {
            m_mod = CAL_MAX_MOD;
        }
        Telemetry::printf("[CAL ENC] using provided breakaway mod %.3f -> hold mod %.3f",
                          static_cast<double>(breakaway_mod),
                          static_cast<double>(m_mod));
        enterState(State::SETTLE);
    } else {
        enterState(State::FIND_VOLTAGE);
    }

    return true;
}

void EncoderOffsetCalibrator::enterState(State state) {
    m_state = state;
    m_state_start_ms = HAL_GetTick();

    Telemetry::printf("[CAL ENC DBG] enter %s", stateName(state));

    switch (state) {
        case State::FIND_VOLTAGE:
            /* Spin the rotor at 1 Hz while ramping modulation.  Movement of the
             * encoder (as opposed to just electrical cycles) tells us the
             * breakaway voltage. */
            PWM_ResetSPWMElectricalCycles();
            m_last_ramp_ms = HAL_GetTick();
            m_mod = 0.0f;
            PWM_StartSPWM(ROTATION_FREQUENCY_HZ, 0.0f);
            break;
        case State::SETTLE:
            m_settle_start_ms = HAL_GetTick();
            m_settle_angle = m_unwrapped_angle;
            break;
        case State::HOLD:
            holdCurrentAngle();
            break;
        case State::ROTATE:
            /* Rotate the electrical field.  We stop after at least one full
             * cycle when the angle is at 90 deg (phase U high) and the rotor
             * has moved past the midpoint between pole-pair alignments. */
            PWM_ResetSPWMElectricalCycles();
            m_elec_cycles_start = PWM_GetSPWMElectricalCycles();
            m_rotate_start_angle = m_unwrapped_angle;
            PWM_StartSPWM(ROTATION_FREQUENCY_HZ, m_mod);
            break;
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
    restoreHardware();

    va_list ap;
    va_start(ap, reason_fmt);
    Telemetry::vprintf(reason_fmt, ap);
    va_end(ap);

    m_state = State::FAIL;
}

void EncoderOffsetCalibrator::sampleEncoderAngle() {
    float angle = 0.0f;
    if (encoderADC().sample(angle)) {
        float delta = angle - m_last_angle;
        if (delta > 180.0f) {
            delta -= 360.0f;
        } else if (delta < -180.0f) {
            delta += 360.0f;
        }
        m_unwrapped_angle += delta;
        m_last_angle = angle;
    }
}

bool EncoderOffsetCalibrator::isEncoderSettled() const {
    const float delta_cycles = std::fabs((m_unwrapped_angle - m_settle_angle) / 360.0f);
    if (delta_cycles > SETTLE_THRESHOLD_CYCLES) {
        return false;
    }
    const uint32_t now_ms = HAL_GetTick();
    return (now_ms - m_settle_start_ms) >= SETTLE_STABLE_MS;
}

void EncoderOffsetCalibrator::holdCurrentAngle() {
    /* Always align to the same electrical vector: phase U high, V and W low.
     * We use the maximum safe modulation for holding so cogging/friction cannot
     * keep the rotor away from the true alignment.  The sinusoidal
     * voltage-angle function keeps all three phases switching so bootstrap
     * capacitors on the high-side gate drivers stay charged. */
    PWM_StopSPWM();
    PWM_SetVoltageAngle(VOLTAGE_ANGLE_U_HIGH_RAD, CAL_MAX_MOD);
}

float EncoderOffsetCalibrator::wrapOffset(float offset, float period) {
    /* Mathematical modulo into [0, period).  Using floor avoids any
     * sign ambiguity from std::fmod on negative inputs. */
    float wrapped = offset - period * std::floor(offset / period);
    if (wrapped < 0.0f) {
        wrapped += period;
    }
    if (wrapped >= period) {
        wrapped -= period;
    }
    return wrapped;
}

void EncoderOffsetCalibrator::measureAndLog() {
    /* Convert encoder electrical angle to true mechanical angle, then take the
     * offset as the mechanical angle modulo one motor pole-pair step. */
    const float mech_angle = m_unwrapped_angle / m_encoder_cycles_per_rev;
    const float wrapped = wrapOffset(mech_angle, m_mech_deg_per_motor_elec_cycle);

    if (m_sample_count < MAX_STEPS) {
        m_offsets[m_sample_count] = wrapped;
    }
    m_sum_offset += wrapped;
    ++m_sample_count;

    Telemetry::printf("[CAL ENC] sample %d/%d: step_idx=%d enc_elec=%.3f enc_mech=%.3f step=%.3f offset=%.3f deg",
                      m_sample_count,
                      m_total_steps,
                      m_step,
                      static_cast<double>(m_unwrapped_angle),
                      static_cast<double>(mech_angle),
                      static_cast<double>(m_mech_deg_per_motor_elec_cycle),
                      static_cast<double>(wrapped));
}

void EncoderOffsetCalibrator::update() {
    if (m_state == State::IDLE || m_state == State::DONE || m_state == State::FAIL) {
        return;
    }

    sampleEncoderAngle();
    const uint32_t now_ms = HAL_GetTick();

    switch (m_state) {
        case State::FIND_VOLTAGE: {
            /* Spin at 1 Hz and ramp modulation until the encoder follows.
             * This works even if the rotor is already aligned, because a
             * rotating field will drag it once the voltage is high enough. */
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
                PWM_SetSPWMParams(ROTATION_FREQUENCY_HZ, m_mod);
                Telemetry::printf("[CAL ENC DBG] ramp mod=%.3f angle_cycles=%.3f",
                                  static_cast<double>(m_mod),
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
                Telemetry::printf("[CAL ENC] breakaway at mod=%.3f -> hold mod=%.3f (angle_cycles=%.3f)",
                                  static_cast<double>(m_breakaway_mod),
                                  static_cast<double>(m_mod),
                                  static_cast<double>(angle_cycles));
                enterState(State::SETTLE);
                return;
            }

            if (m_mod >= CAL_MAX_MOD && (now_ms - m_last_move_ms) > MOVE_TIMEOUT_MS) {
                PWM_StopSPWM();
                fail("[CAL ENC] FAIL: rotor did not move during 1 Hz voltage ramp");
                return;
            }

            if ((now_ms - m_state_start_ms) > FIND_VOLTAGE_TIMEOUT_MS) {
                PWM_StopSPWM();
                fail("[CAL ENC] FAIL: voltage ramp timed out");
                return;
            }
            break;
        }

        case State::SETTLE: {
            holdCurrentAngle();
            const float settle_delta_cycles = std::fabs((m_unwrapped_angle - m_settle_angle) / 360.0f);
            if (settle_delta_cycles > SETTLE_THRESHOLD_CYCLES) {
                /* Still moving; reset the stable timer. */
                m_settle_start_ms = now_ms;
                m_settle_angle = m_unwrapped_angle;
            }
            if ((now_ms - m_last_dbg_ms) >= 500U) {
                m_last_dbg_ms = now_ms;
                Telemetry::printf("[CAL ENC DBG] settle: enc_elec=%.3f enc_mech=%.3f delta_cycles=%.3f stable_ms=%lu",
                                  static_cast<double>(m_unwrapped_angle),
                                  static_cast<double>(m_unwrapped_angle / m_encoder_cycles_per_rev),
                                  static_cast<double>(settle_delta_cycles),
                                  static_cast<unsigned long>(now_ms - m_settle_start_ms));
            }
            if ((now_ms - m_settle_start_ms) >= SETTLE_STABLE_MS) {
                enterState(State::HOLD);
                return;
            }
            if ((now_ms - m_state_start_ms) > SETTLE_TIMEOUT_MS) {
                fail("[CAL ENC] FAIL: encoder did not settle (still moving %.2f cycles after %lu ms)",
                     static_cast<double>(settle_delta_cycles),
                     static_cast<unsigned long>(now_ms - m_state_start_ms));
                return;
            }
            break;
        }

        case State::HOLD: {
            holdCurrentAngle();
            if ((now_ms - m_last_dbg_ms) >= 500U) {
                m_last_dbg_ms = now_ms;
                Telemetry::printf("[CAL ENC DBG] hold: enc_elec=%.3f enc_mech=%.3f hold_ms=%lu",
                                  static_cast<double>(m_unwrapped_angle),
                                  static_cast<double>(m_unwrapped_angle / m_encoder_cycles_per_rev),
                                  static_cast<unsigned long>(now_ms - m_state_start_ms));
            }
            if ((now_ms - m_state_start_ms) >= HOLD_SETTLE_MS) {
                measureAndLog();
                ++m_step;
                if (m_step >= m_total_steps) {
                    m_average_offset = (m_sample_count > 0)
                                           ? (m_sum_offset / static_cast<float>(m_sample_count))
                                           : 0.0f;
                    m_average_offset = wrapOffset(m_average_offset, m_mech_deg_per_motor_elec_cycle);

                    float min_off = m_offsets[0];
                    float max_off = m_offsets[0];
                    for (int i = 1; i < m_sample_count; ++i) {
                        if (m_offsets[i] < min_off) min_off = m_offsets[i];
                        if (m_offsets[i] > max_off) max_off = m_offsets[i];
                    }
                    restoreHardware();
                    Telemetry::printf("[CAL ENC] DONE: avg=%.3f deg min=%.3f deg max=%.3f deg range=%.3f deg over %d samples",
                                      static_cast<double>(m_average_offset),
                                      static_cast<double>(min_off),
                                      static_cast<double>(max_off),
                                      static_cast<double>(max_off - min_off),
                                      m_sample_count);
                    m_state = State::DONE;
                    return;
                }
                enterState(State::ROTATE);
                return;
            }
            if ((now_ms - m_state_start_ms) > SETTLE_TIMEOUT_MS) {
                fail("[CAL ENC] FAIL: hold timed out");
                return;
            }
            break;
        }

        case State::ROTATE: {
            /* Rotate until we have completed at least one electrical cycle, the
             * field is back at the U-high electrical angle (90 deg), and the
             * rotor has moved past the midpoint to the next pole-pair position.
             * Stopping at the U-high angle means holdCurrentAngle() does not
             * cause an abrupt voltage-vector jump. */
            const uint32_t cycles = PWM_GetSPWMElectricalCycles();
            const float moved = std::fabs(m_unwrapped_angle - m_rotate_start_angle);
            const float angle = PWM_GetSPWMAngle();
            const bool angle_ok = std::fabs(angle - VOLTAGE_ANGLE_U_HIGH_RAD) <= ANGLE_STOP_TOLERANCE_RAD;
            const bool moved_enough = moved >= (0.5f * m_enc_deg_per_motor_elec_cycle);
            const bool one_cycle = (cycles - m_elec_cycles_start) >= 1U;

            /* Log every 500 ms so we can see the rotation profile. */
            if ((now_ms - m_last_dbg_ms) >= 500U) {
                m_last_dbg_ms = now_ms;
                Telemetry::printf("[CAL ENC DBG] rotate: cycles=%lu/%lu angle=%.2f rad moved_elec=%.2f moved_mech=%.2f angle_ok=%d moved_ok=%d",
                                  static_cast<unsigned long>(cycles),
                                  static_cast<unsigned long>(m_elec_cycles_start + 1U),
                                  static_cast<double>(angle),
                                  static_cast<double>(moved),
                                  static_cast<double>(moved / m_encoder_cycles_per_rev),
                                  static_cast<int>(angle_ok),
                                  static_cast<int>(moved_enough));
            }

            if (one_cycle && angle_ok && moved_enough) {
                PWM_StopSPWM();
                Telemetry::printf("[CAL ENC DBG] rotate done: cycles=%lu angle=%.2f rad moved_elec=%.2f moved_mech=%.2f deg",
                                  static_cast<unsigned long>(cycles),
                                  static_cast<double>(angle),
                                  static_cast<double>(moved),
                                  static_cast<double>(moved / m_encoder_cycles_per_rev));
                enterState(State::SETTLE);
                return;
            }
            if ((now_ms - m_state_start_ms) > ROTATE_TIMEOUT_MS) {
                PWM_StopSPWM();
                fail("[CAL ENC] FAIL: rotation timed out (moved %.2f deg, angle=%.2f rad, cycles=%lu)",
                     static_cast<double>(moved),
                     static_cast<double>(angle),
                     static_cast<unsigned long>(cycles));
                return;
            }
            break;
        }

        default:
            break;
    }
}

} // namespace Inverter
