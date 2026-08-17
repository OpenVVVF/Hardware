#include "Inverter/Control/FocControlManager.h"

#include "tim.h"

#include "Inverter/Calibration/AutoCalibrationCoordinator.h"
#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Calibration/MotorCalibration.h"
#include "Inverter/Calibration/PoleCalibrator.h"
#include "Inverter/Calibration/ResistanceCalibrator.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Telemetry.h"

#include "main.h"

#include <cmath>

namespace Inverter {

static FocControlManager s_instance;

FocControlManager& focControlManager() {
    return s_instance;
}

bool FocControlManager::init() {
    if (m_initialized) {
        return true;
    }

    m_config.Kp_D = DEFAULT_KP;
    m_config.Ki_D = DEFAULT_KI;
    m_config.Kp_Q = DEFAULT_KP;
    m_config.Ki_Q = DEFAULT_KI;
    m_config.SoftVoltageLimit_V = DEFAULT_SOFT_VOLTAGE_LIMIT_V;
    m_config.MaxPhaseCurrent_A = 200.0f;
    m_config.MaxModulation = 0.9f;
    m_controller.ApplyConfig(m_config);

    m_setpoints = {};
    m_motor = {};
    m_dt_s = 0.0f;

    m_initialized = true;
    m_running = false;
    m_starting = false;
    m_stop_requested_from_isr = false;
    m_startup_state = StartupState::IDLE;
    m_missed_current_samples = 0;

    Telemetry::printf("[FOC] initialized");
    return true;
}

static void pollGateDriverStatus() {
    if (!GateDriver_IsReady()) {
        FaultManager::instance().raise(FaultSource::GateDriverUvlo,
                                       FaultReason::GateDriverNotReady);
    }
}

bool FocControlManager::isAnyCalibrationActive() const {
    return poleCalibrator().isActive() ||
           encoderOffsetCalibrator().isActive() ||
           resistanceCalibrator().isActive() ||
           autoCalibrationCoordinator().isActive();
}

bool FocControlManager::checkSensorReadiness() {
    if (!phaseCurrentADC().offsetValid()) {
        Telemetry::printf("[FOC] ERROR: phase-current offsets not valid; run cal");
        return false;
    }

    if (!encoderADC().boundsValid()) {
        Telemetry::printf("[FOC] ERROR: encoder bounds not valid; run encodercal");
        return false;
    }

    if (!dcLinkVoltageSensor().hasSample()) {
        Telemetry::printf("[FOC] ERROR: no DC-link voltage sample yet");
        return false;
    }

    const float vdc = dcLinkVoltageSensor().voltage();
    if (vdc < MIN_VDC_V) {
        Telemetry::printf("[FOC] ERROR: Vdc %.2f V below minimum %.2f V",
                          static_cast<double>(vdc), static_cast<double>(MIN_VDC_V));
        return false;
    }

    return true;
}

void FocControlManager::applySetpointLimits() {
    float max_a = m_config.MaxPhaseCurrent_A;
    bool clamped = false;

    // Clamp individual axes.
    if (m_setpoints.id_a > max_a) { m_setpoints.id_a = max_a; clamped = true; }
    if (m_setpoints.id_a < -max_a) { m_setpoints.id_a = -max_a; clamped = true; }
    if (m_setpoints.iq_a > max_a) { m_setpoints.iq_a = max_a; clamped = true; }
    if (m_setpoints.iq_a < -max_a) { m_setpoints.iq_a = -max_a; clamped = true; }

    // Circular limit.
    float mag_sq = m_setpoints.id_a * m_setpoints.id_a +
                   m_setpoints.iq_a * m_setpoints.iq_a;
    float max_sq = max_a * max_a;
    if (mag_sq > max_sq && mag_sq > 1e-12f) {
        float scale = max_a / std::sqrt(mag_sq);
        m_setpoints.id_a *= scale;
        m_setpoints.iq_a *= scale;
        clamped = true;
    }

    if (clamped && !m_limit_warning_logged) {
        Telemetry::printf("[FOC] setpoint clamped to +/-%.1f A", static_cast<double>(max_a));
        m_limit_warning_logged = true;
    } else if (!clamped) {
        m_limit_warning_logged = false;
    }
}

bool FocControlManager::start(float iq_a, float id_a, bool allow_during_cal) {
    if (!m_initialized) {
        Telemetry::printf("[FOC] ERROR: not initialized");
        return false;
    }

    if (m_running || m_starting) {
        stop();
    }

    if (!MotorCalibration::instance().valid) {
        Telemetry::printf("[FOC] ERROR: motor calibration invalid; run motorcal first");
        return false;
    }

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::printf("[FOC] ERROR: active Critical/High faults, cannot start");
        FaultManager::instance().printSummary();
        return false;
    }

    if (openLoopController().isRunning()) {
        Telemetry::printf("[FOC] stopping open-loop controller first");
        openLoopController().stop();
    }

    if (!allow_during_cal && isAnyCalibrationActive()) {
        Telemetry::printf("[FOC] ERROR: calibration is active");
        return false;
    }

    // Make sure the sensors we need are actually producing data.
    if (!checkSensorReadiness()) {
        return false;
    }

    // Resolve motor parameters from calibration.
    float vdc = dcLinkVoltageSensor().voltage();
    if (vdc < MIN_VDC_V) {
        Telemetry::printf("[FOC] ERROR: Vdc %.2f V below minimum %.2f V",
                          static_cast<double>(vdc), static_cast<double>(MIN_VDC_V));
        return false;
    }
    m_motor = buildMotorParametersFromCalibration(MotorCalibration::instance(), vdc);
    /* Apply any runtime offset adjustment the user has tuned via the shell. */
    const float adj_elec_rad = m_encoder_offset_adjustment_deg *
                               (3.14159265358979323846f / 180.0f) *
                               m_motor.pole_pairs / m_motor.encoder_cycles_per_rev;
    m_motor.encoder_offset_rad += adj_elec_rad;
    m_controller.SetMotorParameters(m_motor);

    // Resolve control-loop period for the controller.
    float f_update = PWM_GetUpdateFrequency();
    if (f_update <= 0.0f) {
        f_update = 5000.0f;
    }
    m_dt_s = 1.0f / f_update;

    m_setpoints.id_a = id_a;
    m_setpoints.iq_a = iq_a;
    m_forced_angle = false;
    applySetpointLimits();

    // Begin gate-driver startup sequence.
    m_starting = true;
    m_running = false;
    m_startup_state = StartupState::RESET_ASSERT;
    m_startup_start_ms = HAL_GetTick();

    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
    PWM_ClearFault();
    GateDriver_DisableOutputs();
    m_startup_wait_until_ms = HAL_GetTick() + RESET_ASSERT_MS;

    Telemetry::printf("[FOC] startup sequence started (iq=%.2f A id=%.2f A)",
                      static_cast<double>(m_setpoints.iq_a),
                      static_cast<double>(m_setpoints.id_a));
    return true;
}

void FocControlManager::stepStartup(uint32_t now_ms) {
    switch (m_startup_state) {
        case StartupState::RESET_ASSERT:
            if ((int32_t)(now_ms - m_startup_wait_until_ms) >= 0) {
                HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin,
                                  GPIO_PIN_SET);
                m_startup_wait_until_ms = now_ms + RESET_RELEASE_MS;
                m_startup_state = StartupState::RESET_RELEASE;
            }
            break;

        case StartupState::RESET_RELEASE:
            if ((int32_t)(now_ms - m_startup_wait_until_ms) >= 0) {
                m_startup_state = StartupState::WAIT_READY;
            }
            break;

        case StartupState::WAIT_READY: {
            bool ready = GateDriver_IsReady();
            bool fault = GateDriver_IsFault();
            if (ready && !fault) {
                PWM_EnableFocMode();
                PWM_Start();
                if ((htim1.Instance->BDTR & TIM_BDTR_MOE) == 0U) {
                    Telemetry::printf("[FOC] ERROR: TIM1 MOE not active after PWM start");
                    GateDriver_DisableOutputs();
                    FaultManager::instance().raise(FaultSource::PwmBreak,
                                                   FaultReason::GateDriverNotReady);
                    m_starting = false;
                    m_running = false;
                    m_startup_state = StartupState::IDLE;
                    break;
                }
                /* Reset controller state before the first ISR fires so the
                 * integrators and angle estimates start from a known zero. */
                m_controller.Reset();
                m_missed_current_samples = 0;
                PWM_StartUpdateInterrupt();
                m_startup_state = StartupState::STARTED;
                m_running = true;
                m_starting = false;
                Telemetry::printf("[FOC] STARTED f_sw=%.0f Hz f_u=%.0f Hz dt=%.4f ms",
                                  static_cast<double>(PWM_GetFrequency()),
                                  static_cast<double>(1.0f / m_dt_s),
                                  static_cast<double>(m_dt_s * 1000.0f));
            } else if (fault || (now_ms - m_startup_start_ms) > STARTUP_TIMEOUT_MS) {
                Telemetry::printf("[FOC] ERROR: gate driver not ready or fault latched");
                GateDriver_DisableOutputs();
                FaultManager::instance().raise(FaultSource::GateDriverUvlo,
                                               FaultReason::GateDriverNotReady);
                m_starting = false;
                m_running = false;
                m_startup_state = StartupState::IDLE;
            }
            break;
        }

        case StartupState::STARTED:
        case StartupState::IDLE:
        default:
            break;
    }
}

void FocControlManager::stop() {
    PWM_DisableFocMode();
    PWM_StopUpdateInterrupt();
    PWM_StopSPWM();
    GateDriver_DisableOutputs();
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);

    m_controller.Reset();

    m_running = false;
    m_starting = false;
    m_startup_state = StartupState::IDLE;

    Telemetry::printf("[FOC] STOPPED");
}

void FocControlManager::setId(float id_a) {
    __disable_irq();
    m_setpoints.id_a = id_a;
    applySetpointLimits();
    __enable_irq();
}

void FocControlManager::setIq(float iq_a) {
    __disable_irq();
    m_setpoints.iq_a = iq_a;
    applySetpointLimits();
    __enable_irq();
}

void FocControlManager::setKp(float kp) {
    if (kp < 0.0f) kp = 0.0f;
    __disable_irq();
    m_config.Kp_D = kp;
    m_config.Kp_Q = kp;
    m_controller.ApplyConfig(m_config);
    __enable_irq();
    Telemetry::printf("[FOC] Kp set to %.4f", static_cast<double>(kp));
}

void FocControlManager::setKi(float ki) {
    if (ki < 0.0f) ki = 0.0f;
    __disable_irq();
    m_config.Ki_D = ki;
    m_config.Ki_Q = ki;
    m_controller.ApplyConfig(m_config);
    __enable_irq();
    Telemetry::printf("[FOC] Ki set to %.4f", static_cast<double>(ki));
}

void FocControlManager::setVoltageLimit(float v_v) {
    __disable_irq();
    m_config.SoftVoltageLimit_V = (v_v < 0.0f) ? 0.0f : v_v;
    m_controller.ApplyConfig(m_config);
    __enable_irq();
    Telemetry::printf("[FOC] voltage limit set to %.2f V", static_cast<double>(m_config.SoftVoltageLimit_V));
}

void FocControlManager::adjustEncoderOffset(float delta_mech_deg) {
    m_encoder_offset_adjustment_deg += delta_mech_deg;

    /* Update the active controller if FOC is already running. */
    __disable_irq();
    MotorParameters p = m_motor;
    const float adj_elec_rad = m_encoder_offset_adjustment_deg *
                               (3.14159265358979323846f / 180.0f) *
                               p.pole_pairs / p.encoder_cycles_per_rev;
    /* Re-derive from the calibration base so repeated calls do not accumulate
     * scaling errors. */
    p = buildMotorParametersFromCalibration(MotorCalibration::instance(), p.vdc_v);
    p.encoder_offset_rad += adj_elec_rad;
    m_motor = p;
    m_controller.SetMotorParameters(p);
    __enable_irq();

    Telemetry::printf("[FOC] encoder offset adjusted to %.3f mech deg (delta %.3f)",
                      static_cast<double>(encoderOffsetDeg()),
                      static_cast<double>(delta_mech_deg));
}

void FocControlManager::resetEncoderOffsetAdjustment() {
    __disable_irq();
    m_encoder_offset_adjustment_deg = 0.0f;
    if (m_running) {
        MotorParameters p =
            buildMotorParametersFromCalibration(MotorCalibration::instance(), m_motor.vdc_v);
        m_motor = p;
        m_controller.SetMotorParameters(p);
    }
    __enable_irq();

    Telemetry::printf("[FOC] encoder offset adjustment reset");
}

void FocControlManager::setForcedAngleRate(float elec_hz) {
    bool active = false;
    __disable_irq();
    if (elec_hz != 0.0f && m_running) {
        /* Ramp the Park-transform angle at the requested electrical rate.  The
         * angle fed to the controller is a raw encoder angle, so the ramp runs
         * in encoder counts: elec_hz * cycles-per-rev / pole-pairs. */
        const float cycles = (m_motor.encoder_cycles_per_rev > 1e-6f)
                                 ? m_motor.encoder_cycles_per_rev : 1.0f;
        m_forced_enc_angle_rad = 0.0f;
        m_forced_rate_rad_per_s =
            (2.0f * 3.14159265358979323846f * elec_hz) * cycles / m_motor.pole_pairs;
        m_motor.encoder_sign = 1.0f;
        m_motor.encoder_offset_rad = 0.0f;
        m_controller.SetMotorParameters(m_motor);
        m_forced_angle = true;
        active = true;
    } else {
        m_forced_angle = false;
        if (m_running) {
            /* Restore normal encoder feedback: rebuild parameters from
             * calibration plus any runtime offset adjustment. */
            MotorParameters p =
                buildMotorParametersFromCalibration(MotorCalibration::instance(), m_motor.vdc_v);
            const float adj_elec_rad = m_encoder_offset_adjustment_deg *
                                       (3.14159265358979323846f / 180.0f) *
                                       p.pole_pairs / p.encoder_cycles_per_rev;
            p.encoder_offset_rad += adj_elec_rad;
            m_motor = p;
            m_controller.SetMotorParameters(p);
        }
    }
    __enable_irq();
    Telemetry::printf("[FOC] forced-angle %s (%.2f elec Hz)",
                      active ? "ENABLED" : "disabled",
                      static_cast<double>(elec_hz));
}

void FocControlManager::setEncoderSign(float sign) {
    float s = (sign >= 0.0f) ? 1.0f : -1.0f;

    __disable_irq();
    MotorCalibration::instance().encoder_sign = s;
    MotorParameters p = buildMotorParametersFromCalibration(MotorCalibration::instance(), m_motor.vdc_v);
    const float adj_elec_rad = m_encoder_offset_adjustment_deg *
                               (3.14159265358979323846f / 180.0f) *
                               p.pole_pairs / p.encoder_cycles_per_rev;
    p.encoder_offset_rad += adj_elec_rad;
    m_motor = p;
    m_controller.SetMotorParameters(p);
    __enable_irq();

    Telemetry::printf("[FOC] encoder sign set to %.0f", static_cast<double>(s));
}

float FocControlManager::encoderOffsetDeg() const {
    return m_encoder_offset_adjustment_deg +
           MotorCalibration::instance().encoder_offset_deg;
}

void FocControlManager::onPwmPeriod() {
    if (!m_running) {
        return;
    }

    /* Atomically consume the latest synchronous current sample.  If no new
     * sample is available the ADC trigger may have been lost; count misses and
     * shut down before the controller runs open-loop. */
    float iu = 0.0f;
    float iv = 0.0f;
    float iw = 0.0f;
    if (!phaseCurrentADC().sample(iu, iv, iw)) {
        if (++m_missed_current_samples >= MAX_MISSED_CURRENT_SAMPLES) {
            FaultManager::instance().raise(FaultSource::AdcError,
                                           FaultReason::AdcHalError);
            requestSafeStopFromIsr();
        }
        return;
    }
    m_missed_current_samples = 0;

    /* The phase-current sensors on this hardware are wired with inverted
     * polarity relative to the FOC convention.  Negate all three phase
     * currents so the Clarke/Park transforms see the correct sign. */
    iu = -iu;
    iv = -iv;
    iw = -iw;

    /* Snapshot raw phase currents for telemetry. */
    m_last_iu_a = iu;
    m_last_iv_a = iv;
    m_last_iw_a = iw;

    /* DC-link voltage must be fresh and above the safe operating threshold.
     * The sensor is updated in the main loop; reading m_voltage here is a
     * single volatile float access and therefore atomic on this Cortex-M7. */
    float vdc = dcLinkVoltageSensor().voltage();
    if (!dcLinkVoltageSensor().hasSample() || vdc < MIN_VDC_V ||
        std::isnan(vdc) || std::isinf(vdc)) {
        FaultManager::instance().raise(FaultSource::Max22530Uv,
                                       FaultReason::Max22530Undervoltage);
        requestSafeStopFromIsr();
        return;
    }

    /* The encoder angle must be fresh: a stalled encoder stream freezes the
     * Park angle, so the rotor aligns to the fixed commanded field vector and
     * locks while id/iq keep regulating.  Sample age comes from the DMA
     * completion tick, so a busy main loop cannot cause false trips. */
    if ((HAL_GetTick() - encoderADC().lastSampleMs()) > ENCODER_STALE_MS) {
        FaultManager::instance().raise(FaultSource::EncoderTimeout,
                                       FaultReason::EncoderSampleTimeout);
        requestSafeStopFromIsr();
        return;
    }

    float angle_deg = encoderADC().lastAngle();
    float angle_rad = angle_deg * (3.14159265358979323846f / 180.0f);

    /* Forced-angle diagnostic: ignore the encoder and drive the Park angle
     * from a software ramp (see setForcedAngleRate).  State is kept in
     * [0, 2pi) and fed directly, matching the encoder-angle convention. */
    if (m_forced_angle) {
        m_forced_enc_angle_rad += m_forced_rate_rad_per_s * m_dt_s;
        m_forced_enc_angle_rad = std::fmod(m_forced_enc_angle_rad,
                                           2.0f * 3.14159265358979323846f);
        if (m_forced_enc_angle_rad < 0.0f) {
            m_forced_enc_angle_rad += 2.0f * 3.14159265358979323846f;
        }
        angle_rad = m_forced_enc_angle_rad;
    }

    /* Copy setpoints atomically so a main-loop update cannot give us a
     * partially-written id/iq pair. */
    FocSetpoints set;
    __disable_irq();
    set = m_setpoints;
    __enable_irq();

    FocInputs in;
    in.iu_a = iu;
    in.iv_a = iv;
    in.iw_a = iw;
    in.vdc_v = vdc;
    in.encoder_angle_rad = angle_rad;
    in.encoder_velocity_rad_per_s = 0.0f;

    FocOutputs out = m_controller.Update(in, set, m_dt_s);

    /* If the controller produced non-finite voltages, park at zero vector and
     * let the main loop shut down cleanly. */
    if (std::isnan(out.valpha_v) || std::isinf(out.valpha_v) ||
        std::isnan(out.vbeta_v) || std::isinf(out.vbeta_v) ||
        std::isnan(out.vd_v) || std::isinf(out.vd_v) ||
        std::isnan(out.vq_v) || std::isinf(out.vq_v)) {
        FaultManager::instance().raise(FaultSource::PhaseOvercurrent,
                                       FaultReason::PhaseOvercurrentSoftware);
        requestSafeStopFromIsr();
        return;
    }

    PWM_SetVoltageVector(out.valpha_v, out.vbeta_v, vdc);

    if (m_sample_cb != nullptr) {
        m_sample_cb(m_controller.Id_A, m_controller.Iq_A,
                    m_controller.Vd_V, m_controller.Vq_V, m_sample_cb_ctx);
    }
}

void FocControlManager::requestSafeStopFromIsr() {
    /* Never call the full stop() routine from an ISR: it uses HAL delays and
     * UART printfs.  Park at zero vector and let the main loop perform the
     * full shutdown. */
    PWM_SetVoltageVector(0.0f, 0.0f, dcLinkVoltageSensor().voltage());
    m_running = false;
    m_stop_requested_from_isr = true;
}

void FocControlManager::logTelemetry() {
    Telemetry::log("foc_running", m_running ? 1.0f : 0.0f);
    Telemetry::log("foc_id_cmd", m_setpoints.id_a);
    Telemetry::log("foc_iq_cmd", m_setpoints.iq_a);
    Telemetry::log("foc_id", m_controller.Id_A);
    Telemetry::log("foc_iq", m_controller.Iq_A);
    Telemetry::log("foc_vd", m_controller.Vd_V);
    Telemetry::log("foc_vq", m_controller.Vq_V);
    Telemetry::log("foc_valpha", m_controller.Valpha_V);
    Telemetry::log("foc_vbeta", m_controller.Vbeta_V);
    Telemetry::log("foc_elec_angle", m_controller.ElectricalAngle_Rad);
    Telemetry::log("foc_elec_speed", m_controller.ElectricalSpeed_RadPerSec);
    Telemetry::log("foc_vdc", dcLinkVoltageSensor().voltage());
    Telemetry::log("foc_iu", m_last_iu_a);
    Telemetry::log("foc_iv", m_last_iv_a);
    Telemetry::log("foc_iw", m_last_iw_a);
    Telemetry::log("foc_missed", static_cast<float>(m_missed_current_samples));

    /* Duty-cycle readback: proves whether the SVPWM CCR writes actually reach
     * the timer in FOC mode (vs. frozen duties driving a fixed-axis field). */
    const uint32_t arr = TIM1->ARR;
    if (arr > 0U) {
        Telemetry::log("foc_du", 100.0f * static_cast<float>(TIM1->CCR1) / static_cast<float>(arr));
        Telemetry::log("foc_dv", 100.0f * static_cast<float>(TIM1->CCR2) / static_cast<float>(arr));
        Telemetry::log("foc_dw", 100.0f * static_cast<float>(TIM1->CCR3) / static_cast<float>(arr));
    }
}

void FocControlManager::update() {
    const uint32_t now_ms = HAL_GetTick();

    if (m_stop_requested_from_isr) {
        m_stop_requested_from_isr = false;
        Telemetry::printf("[FOC] stop requested from ISR - shutting down");
        FaultManager::instance().printSummary();
        stop();
        return;
    }

    if (m_starting) {
        stepStartup(now_ms);
        return;
    }

    if (!m_running) {
        return;
    }

    /* The switching/update frequency is a runtime variable: if it changed
     * since we computed the control-loop period, recompute it so the PI
     * timing stays correct. */
    {
        const float f_update = PWM_GetUpdateFrequency();
        if (f_update > 0.0f && m_dt_s > 0.0f &&
            std::fabs(f_update - 1.0f / m_dt_s) > 0.5f) {
            m_dt_s = 1.0f / f_update;
            Telemetry::printf("[FOC] update rate changed; dt=%.4f ms",
                              static_cast<double>(m_dt_s * 1000.0f));
        }
    }

    pollGateDriverStatus();

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical)) {
        Telemetry::printf("[FOC] Critical fault detected - stopping");
        FaultManager::instance().printSummary();
        stop();
        return;
    }

    logTelemetry();
}

} // namespace Inverter

extern "C" void FocControlManager_OnPwmPeriod(void) {
    Inverter::focControlManager().onPwmPeriod();
}
