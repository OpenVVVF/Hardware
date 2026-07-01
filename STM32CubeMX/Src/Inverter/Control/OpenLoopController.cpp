#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"

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

void OpenLoopController::rampModulation(float from_m, float to_m, uint32_t ramp_ms) {
    const int steps = 20;
    const int step_ms = static_cast<int>(ramp_ms / steps);
    if (step_ms <= 0) {
        PWM_SetSPWMParams(m_freq_hz, to_m);
        return;
    }

    for (int i = 1; i <= steps; ++i) {
        /* High/Warning faults do not abort a running ramp under Option A.
         * Critical faults are handled by FaultManager::executeSafetyActions()
         * which forces a TIM1 break and disables the gate driver power. */
        float m = from_m + (to_m - from_m) * static_cast<float>(i) / static_cast<float>(steps);
        PWM_SetSPWMParams(m_freq_hz, m);
        HAL_Delay(step_ms);
    }
}

void OpenLoopController::applyModulation(float modulation_index) {
    if (modulation_index < 0.0f) modulation_index = 0.0f;
    if (modulation_index > 1.154700538f) modulation_index = 1.154700538f;
    m_mod_idx = modulation_index;
    PWM_SetSPWMParams(m_freq_hz, m_mod_idx);
}

bool OpenLoopController::init() {
    if (m_initialized) {
        return true;
    }

    /* 10 kHz switching, 1 us dead time. */
    PWM_SetFrequency(10000U);
    PWM_SetDeadTime(1000U);

    /* Park all phases at 50 % (zero voltage vector). */
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);

    /* Gate-driver reset is active low: keep the power stage disabled from
     * the very beginning so there is never a brief switching burst at boot.
     * The gate-driver power rail was already enabled in InverterMain::init()
     * before the current-sensor offset was captured, and it stays in reset
     * here.  TIM1 PWM outputs are left disabled until start(). */
    HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin, GPIO_PIN_RESET);

    m_initialized = true;
    m_running = false;
    m_freq_hz = 0.0f;
    m_mod_idx = 0.0f;

    Telemetry::printf("[OL] Init done. Outputs disabled until start.");
    return true;
}

bool OpenLoopController::start(float freq_hz, float modulation_index) {
    if (!m_initialized) {
        Telemetry::printf("[OL] ERROR: not initialized");
        return false;
    }

    if (m_running) {
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

    /* Park at 50 % (zero vector) before enabling the gate driver. */
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
    PWM_ClearFault();

    /* Release gate-driver reset and wait for it to become ready. */
    GateDriver_EnableOutputs();

    bool ready = false;
    bool fault = true;
    for (int i = 0; i < 100; ++i) {
        ready = GateDriver_IsReady();
        fault = GateDriver_IsFault();
        if (ready && !fault) break;
        HAL_Delay(5);
    }

    if (!ready || fault) {
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
        return false;
    }

    /* Gate driver is ready; enable the PWM output channels.  The next SPWM
     * update will drive the gate driver.  MOE was already enabled by
     * PWM_ClearFault() above. */
    PWM_Start();

    m_freq_hz = (freq_hz < 0.0f) ? 0.0f : freq_hz;
    m_mod_idx = (modulation_index < 0.0f) ? 0.0f : modulation_index;

    PoleEstimator::instance().setElectricalFrequency(m_freq_hz);

    /* Start the angle ramp at zero modulation, then ramp voltage up. */
    PWM_StartSPWM(m_freq_hz, 0.0f);
    m_running = true;
    rampModulation(0.0f, m_mod_idx, 100U);

    if (!m_running) {
        /* rampModulation aborted because a fault appeared during the ramp. */
        GateDriver_DisableOutputs();
        return false;
    }

    Telemetry::printf("[OL] START f=%.2f m=%.3f", m_freq_hz, m_mod_idx);

    /* Start accumulating encoder mechanical cycles vs current zero crossings.
     * Pass the standstill raw sin/cos so the estimator can learn its DC
     * offsets without relying on the calibrated encoder angle. */
    PoleEstimator::instance().setEnabled(
        true, encoderADC().lastRawSin(), encoderADC().lastRawCos());
    return true;
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
    Telemetry::printf("[OL] STOPPED (coast)");
}

void OpenLoopController::setFrequency(float freq_hz) {
    if (freq_hz < 0.0f) freq_hz = 0.0f;
    m_freq_hz = freq_hz;

    PoleEstimator::instance().setElectricalFrequency(m_freq_hz);

    if (m_running) {
        PWM_SetSPWMParams(m_freq_hz, m_mod_idx);
    }
}

void OpenLoopController::setModulationIndex(float modulation_index) {
    if (modulation_index < 0.0f) modulation_index = 0.0f;

    if (m_running) {
        float old_m = m_mod_idx;
        m_mod_idx = modulation_index;
        rampModulation(old_m, m_mod_idx, 100U);
    } else {
        m_mod_idx = modulation_index;
    }
}

void OpenLoopController::setModulationIndexDirect(float modulation_index) {
    applyModulation(modulation_index);
}

void OpenLoopController::update() {
    if (!m_running) {
        return;
    }

    pollGateDriverStatus();

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical)) {
        Telemetry::printf("[OL] Critical fault detected - stopping");
        FaultManager::instance().printSummary();
        stop();
    }
}

} // namespace Inverter
