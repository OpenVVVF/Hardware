#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Control/FocControlManager.h"

#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PoleEstimator.h"
#include "Inverter/Telemetry.h"

#include "main.h"

namespace Inverter {

static void pollGateDriverStatus() {
    /* /RDY low means the gate-driver supply is below UVLO on either side. */
    if (!GateDriver_IsReady()) {
        FaultManager::instance().raise(FaultSource::GateDriverUvlo,
                                       FaultReason::GateDriverNotReady);
    }
}

static OpenLoopController s_instance;

OpenLoopController& openLoopController() {
    return s_instance;
}

float OpenLoopController::maxPhaseCurrentMagnitude() const {
    const float iu = phaseCurrentADC().lastU();
    const float iv = phaseCurrentADC().lastV();
    const float iw = -(iu + iv);
    float max_i = std::fabs(iu);
    if (std::fabs(iv) > max_i) max_i = std::fabs(iv);
    if (std::fabs(iw) > max_i) max_i = std::fabs(iw);
    return max_i;
}

float OpenLoopController::clampedModulation(float commanded) const {
    return (commanded < m_clamp_mod_ceiling) ? commanded : m_clamp_mod_ceiling;
}

void OpenLoopController::updateCurrentClamp(uint32_t now_ms) {
    const float dt = (m_clamp_last_ms != 0U)
        ? (now_ms - m_clamp_last_ms) * 1.0e-3f : 0.0f;
    m_clamp_last_ms = now_ms;
    if (dt <= 0.0f || dt > 0.5f) {
        return;
    }

    const float i_max = maxPhaseCurrentMagnitude();
    const float limit = m_ramp_current_limit_a;

    if (i_max > limit) {
        /* Over limit: throttle down fast (~63 % per 20 ms). */
        const float decay = 1.0f - std::exp(-dt / 0.020f);
        m_clamp_mod_ceiling -= m_clamp_mod_ceiling * decay;
        if (m_clamp_mod_ceiling < 0.0f) m_clamp_mod_ceiling = 0.0f;
    } else if (i_max < 0.8f * limit && m_clamp_mod_ceiling < m_applied_mod_idx) {
        /* Comfortable again: recover toward the commanded value slowly
         * (~0.5 s time constant) so the next ramp is not starved forever. */
        const float recover = 1.0f - std::exp(-dt / 0.5f);
        m_clamp_mod_ceiling += (m_applied_mod_idx - m_clamp_mod_ceiling) * recover;
    }
}

void OpenLoopController::startRamp(float from_m, float to_m, uint32_t ramp_ms,
                                   float current_limit_a,
                                   bool enable_pole_estimator_on_done) {
    m_ramp_from = from_m;
    m_ramp_to = to_m;
    m_ramp_duration_ms = ramp_ms;
    m_ramp_active_limit = (current_limit_a > 0.0f) ? current_limit_a : DEFAULT_RAMP_CURRENT_LIMIT_A;
    m_ramp_enable_pole_estimator = m_ramp_enable_pole_estimator || enable_pole_estimator_on_done;
    m_ramp_start_ms = HAL_GetTick();
    m_ramp_paused = false;
    m_ramp_pause_start_ms = 0;
    m_ramp_state = RampState::RAMPING;

    if (m_ramp_duration_ms == 0U || std::fabs(from_m - to_m) < 1e-4f) {
        m_applied_mod_idx = to_m;
        PWM_SetSPWMParams(m_freq_hz, clampedModulation(m_applied_mod_idx));
        finishRamp();
    }
}

void OpenLoopController::stepRamp(uint32_t now_ms) {
    if (m_ramp_state != RampState::RAMPING) {
        return;
    }

    uint32_t elapsed = now_ms - m_ramp_start_ms;
    if (elapsed > m_ramp_duration_ms) {
        elapsed = m_ramp_duration_ms;
    }

    float desired_m = m_ramp_from + (m_ramp_to - m_ramp_from) *
                          static_cast<float>(elapsed) /
                          static_cast<float>(m_ramp_duration_ms);

    const float current_limit = m_ramp_active_limit;
    const float resume_threshold = 0.8f * current_limit;
    const float i_max = maxPhaseCurrentMagnitude();
    const bool trying_to_increase = (desired_m > m_applied_mod_idx);

    if (i_max > current_limit && trying_to_increase) {
        if (!m_ramp_paused) {
            m_ramp_paused = true;
            m_ramp_pause_start_ms = now_ms;
            Telemetry::printf("[OL] ramp paused: I=%.2f A limit=%.2f A",
                              static_cast<double>(i_max),
                              static_cast<double>(current_limit));
        }
        desired_m = m_applied_mod_idx;
    } else if (m_ramp_paused && i_max <= resume_threshold) {
        /* Extend the ramp timeline by the pause duration so the ramp does not
         * jump forward when current drops. */
        m_ramp_start_ms += (now_ms - m_ramp_pause_start_ms);
        m_ramp_paused = false;
        m_applied_mod_idx = desired_m;
        Telemetry::printf("[OL] ramp resumed: I=%.2f A limit=%.2f A",
                          static_cast<double>(i_max),
                          static_cast<double>(current_limit));
    } else if (!m_ramp_paused) {
        m_applied_mod_idx = desired_m;
    }

    if (m_ramp_paused && (now_ms - m_ramp_pause_start_ms) >= RAMP_PAUSE_TIMEOUT_MS) {
        Telemetry::printf("[OL] ramp aborted: current stayed above limit");
        cancelRamp();
        stop();
        FaultManager::instance().raise(FaultSource::PhaseOvercurrent,
                                       FaultReason::PhaseOvercurrentSoftware);
        return;
    }

    PWM_SetSPWMParams(m_freq_hz, clampedModulation(m_applied_mod_idx));

    if (elapsed >= m_ramp_duration_ms && !m_ramp_paused) {
        finishRamp();
    }
}

void OpenLoopController::finishRamp() {
    m_ramp_state = RampState::IDLE;
    m_ramp_paused = false;
    m_applied_mod_idx = m_ramp_to;
    PWM_SetSPWMParams(m_freq_hz, clampedModulation(m_applied_mod_idx));

    if (m_ramp_enable_pole_estimator) {
        m_ramp_enable_pole_estimator = false;
        Telemetry::printf("[OL] START f=%.2f m=%.3f", m_freq_hz, m_mod_idx);
        PoleEstimator::instance().setEnabled(
            true, encoderADC().lastRawSin(), encoderADC().lastRawCos());
    }

    Telemetry::printf("[OL] ramp done");
}

void OpenLoopController::cancelRamp() {
    m_ramp_state = RampState::IDLE;
    m_ramp_paused = false;
    m_ramp_enable_pole_estimator = false;
}

void OpenLoopController::setRampCurrentLimit(float amps) {
    if (amps < 0.0f) amps = 0.0f;
    m_ramp_current_limit_a = amps;
}

float OpenLoopController::rampCurrentLimit() const {
    return m_ramp_current_limit_a;
}

void OpenLoopController::applyModulation(float modulation_index) {
    if (modulation_index < 0.0f) modulation_index = 0.0f;
    if (modulation_index > 1.154700538f) modulation_index = 1.154700538f;
    m_mod_idx = modulation_index;
    m_applied_mod_idx = modulation_index;
    cancelRamp();
    PWM_SetSPWMParams(m_freq_hz, clampedModulation(m_applied_mod_idx));
}

bool OpenLoopController::init() {
    if (m_initialized) {
        return true;
    }

    /* 2.5 kHz PWM period (5 kHz effective transistor switching in center-aligned
     * mode), 1 us dead time. */
    PWM_SetFrequency(2500U);
    PWM_SetDeadTime(1000U);

    /* Park all phases at 50 % (zero voltage vector). */
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);

    /* Gate-driver reset is active low: keep the power stage disabled from
     * the very beginning so there is never a brief switching burst at boot.
     * The gate-driver power rail was already enabled in InverterMain::init()
     * before the current-sensor offset was captured, and it stays in reset
     * here. */
    HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin, GPIO_PIN_RESET);

    /* Make sure the gate-driver power rail is explicitly enabled.  This is
     * redundant with InverterMain::init() but preserves the old init sequence
     * that produced accurate offsets. */
    HAL_GPIO_WritePin(GATE_DRIVER_POWER_ENABLE_GPIO_Port,
                      GATE_DRIVER_POWER_ENABLE_Pin,
                      GPIO_PIN_SET);
    HAL_Delay(50);

    /* Start TIM1 PWM outputs while the gate driver is still in reset.
     * The power stage cannot switch, but the gate-driver input pins and the
     * isolated supplies see the same operating load/state as a later manual
     * `cal`.  The current-sensor zero point shifts once this rail is loaded,
     * so recalibrate now rather than relying on the pre-PWM offset. */
    PWM_ClearFault();
    PWM_Start();
    HAL_Delay(100);
    if (!phaseCurrentADC().recalibrateOffsets()) {
        Telemetry::printf("[OL] ERROR: init offset calibration failed");
        return false;
    }

    m_initialized = true;
    m_running = false;
    m_starting = false;
    m_freq_hz = 0.0f;
    m_mod_idx = 0.0f;
    m_applied_mod_idx = 0.0f;
    cancelRamp();

    Telemetry::printf("[OL] Init done. Outputs disabled until start.");
    return true;
}

bool OpenLoopController::recalibrateOffsets() {
    if (!m_initialized) {
        Telemetry::printf("[OL] ERROR: not initialized");
        return false;
    }

    if (m_running || m_starting) {
        Telemetry::printf("[OL] ERROR: stop motor before recalibrating offsets");
        return false;
    }

    /* Park at zero vector and keep the gate driver in reset so no current can
     * flow.  The PWM timer keeps running so the current-sensor ADCs sample in
     * their normal operating state. */
    GateDriver_DisableOutputs();
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
    PWM_ClearFault();
    PWM_Start();
    HAL_Delay(100);

    if (!phaseCurrentADC().recalibrateOffsets()) {
        Telemetry::printf("[OL] ERROR: offset recalibration failed");
        return false;
    }

    Telemetry::printf("[OL] offset recalibrated");
    return true;
}

bool OpenLoopController::start(float freq_hz, float modulation_index) {
    if (!m_initialized) {
        Telemetry::printf("[OL] ERROR: not initialized");
        return false;
    }

    if (focControlManager().isRunning()) {
        Telemetry::printf("[OL] ERROR: FOC is running; stop it first");
        return false;
    }

    if (m_running || m_starting) {
        stop();
    }

    /* Refresh latched fault state from hardware inputs. */
    pollGateDriverStatus();

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::printf("[OL] ERROR: active Critical/High faults, cannot start");
        FaultManager::instance().printSummary();
        return false;
    }

    m_freq_hz = (freq_hz < 0.0f) ? 0.0f : freq_hz;
    m_mod_idx = (modulation_index < 0.0f) ? 0.0f : modulation_index;
    m_applied_mod_idx = 0.0f;

    PoleEstimator::instance().setElectricalFrequency(m_freq_hz);

    /* Begin a non-blocking startup sequence.  The actual gate-driver ready wait
     * and voltage ramp are stepped from update() so telemetry keeps running. */
    m_starting = true;
    m_running = false;
    m_startup_state = StartupState::RESET_ASSERT;
    m_startup_start_ms = HAL_GetTick();

    /* Park at 50 % (zero vector) and assert gate-driver reset before we do
     * anything. */
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
    PWM_ClearFault();
    GateDriver_DisableOutputs();
    m_startup_wait_until_ms = HAL_GetTick() + 10U;

    Telemetry::printf("[OL] startup sequence started");
    return true;
}

void OpenLoopController::stepStartup(uint32_t now_ms) {
    switch (m_startup_state) {
        case StartupState::RESET_ASSERT:
            if ((int32_t)(now_ms - m_startup_wait_until_ms) >= 0) {
                HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin,
                                  GPIO_PIN_SET);
                m_startup_wait_until_ms = now_ms + 10U;
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
                PWM_Start();
                PWM_StartSPWM(m_freq_hz, 0.0f);
                startRamp(0.0f, m_mod_idx, 1000U, m_ramp_current_limit_a, true);
                m_startup_state = StartupState::STARTED;
                m_running = true;
                m_starting = false;
            } else if (fault || (now_ms - m_startup_start_ms) > 500U) {
                uint32_t bdtr = TIM1->BDTR;
                uint32_t sr   = TIM1->SR;
                Telemetry::printf(
                    "[OL] ERROR: gate driver not ready or fault latched | ready=%s fault=%s MOE=%lu BIF=%lu BKF=%lu",
                    ready ? "Y" : "N",
                    fault ? "Y" : "N",
                    (bdtr >> 15) & 1UL,
                    (sr >> 7) & 1UL,
                    (sr >> 6) & 1UL);
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

void OpenLoopController::stop() {
    /* Immediate coast: turn off the PWM outputs and assert the gate-driver
     * reset line so all six IGBTs stop switching right away. */
    PWM_StopSPWM();
    GateDriver_DisableOutputs();

    /* Park at 50 % (zero vector) so the next start begins from a safe state.
     * TIM1 keeps running for current sense. */
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);

    /* Stop pole estimation so coast-down noise/decay does not corrupt
     * the accumulated result.  The last estimate is preserved. */
    PoleEstimator::instance().setEnabled(false);

    m_running = false;
    m_starting = false;
    m_startup_state = StartupState::IDLE;
    cancelRamp();
    Telemetry::printf("[OL] STOPPED (coast)");
}

void OpenLoopController::setFrequency(float freq_hz) {
    if (freq_hz < 0.0f) freq_hz = 0.0f;
    m_freq_hz = freq_hz;

    PoleEstimator::instance().setElectricalFrequency(m_freq_hz);

    if (m_running || m_starting) {
        PWM_SetSPWMParams(m_freq_hz, clampedModulation(m_applied_mod_idx));
    }
}

void OpenLoopController::setModulationIndex(float modulation_index) {
    if (modulation_index < 0.0f) modulation_index = 0.0f;
    m_mod_idx = modulation_index;

    if (m_running || m_starting) {
        startRamp(m_applied_mod_idx, m_mod_idx, 500U, m_ramp_current_limit_a, false);
    } else {
        m_applied_mod_idx = m_mod_idx;
    }
}

void OpenLoopController::setModulationIndexDirect(float modulation_index) {
    applyModulation(modulation_index);
}

void OpenLoopController::update() {
    const uint32_t now_ms = HAL_GetTick();

    if (m_starting) {
        stepStartup(now_ms);
        return;
    }

    if (!m_running) {
        return;
    }

    pollGateDriverStatus();

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical)) {
        Telemetry::printf("[OL] Critical fault detected - stopping");
        FaultManager::instance().printSummary();
        stop();
        return;
    }

    updateCurrentClamp(now_ms);
    stepRamp(now_ms);
}

} // namespace Inverter
