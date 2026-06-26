#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"

#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PolePairEstimator.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include <cstdio>

namespace {

/* newlib-nano vsnprintf may not link %f support, so format floats manually. */
void fmtFloat2(char* buf, size_t cap, float v) {
    int whole = (int)v;
    int frac = (int)((v - whole) * 100.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%02d", whole, frac);
}

void fmtFloat3(char* buf, size_t cap, float v) {
    int whole = (int)v;
    int frac = (int)((v - whole) * 1000.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%03d", whole, frac);
}

} // namespace

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
     * the very beginning so there is never a brief switching burst at boot. */
    HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin, GPIO_PIN_RESET);

    /* Explicitly enable the gate-driver power rail (active high). */
    HAL_GPIO_WritePin(GATE_DRIVER_POWER_ENABLE_GPIO_Port, GATE_DRIVER_POWER_ENABLE_Pin, GPIO_PIN_SET);
    HAL_Delay(50);

    /* Start TIM1 so the ADC current-sense trigger keeps running.
     * The gate driver is still in reset, so the MOSFETs remain off. */
    PWM_ClearFault();
    PWM_Start();

    /* Recalibrate current-sensor offsets now that the gate-driver power rail
     * is up. The sensors share/isolated supplies can shift the zero-current
     * point once that rail is enabled, which is why the pre-power calibration
     * was leaving a residual offset. */
    HAL_Delay(100);
    (void)phaseCurrentADC().recalibrateOffsets();

    m_initialized = true;
    m_running = false;
    m_freq_hz = 0.0f;
    m_mod_idx = 0.0f;

    Telemetry::log("print", "[OL] Init done. Outputs disabled until start.");
    return true;
}

bool OpenLoopController::start(float freq_hz, float modulation_index) {
    if (!m_initialized) {
        Telemetry::log("print", "[OL] ERROR: not initialized");
        return false;
    }

    if (m_running) {
        stop();
    }

    /* Refresh latched fault state from hardware inputs. */
    pollGateDriverStatus();

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::log("print", "[OL] ERROR: active Critical/High faults, cannot start");
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
        Telemetry::log("print", "[OL] ERROR: gate driver not ready or fault latched");
        GateDriver_DisableOutputs();
        FaultManager::instance().raise(FaultSource::GateDriverUvlo,
                                       FaultReason::GateDriverNotReady);
        return false;
    }

    m_freq_hz = (freq_hz < 0.0f) ? 0.0f : freq_hz;
    m_mod_idx = (modulation_index < 0.0f) ? 0.0f : modulation_index;

    PolePairEstimator::instance().setElectricalFrequency(m_freq_hz);

    /* Start the angle ramp at zero modulation, then ramp voltage up. */
    PWM_StartSPWM(m_freq_hz, 0.0f);
    m_running = true;
    rampModulation(0.0f, m_mod_idx, 100U);

    if (!m_running) {
        /* rampModulation aborted because a fault appeared during the ramp. */
        GateDriver_DisableOutputs();
        return false;
    }

    char fbuf[16], mbuf[16];
    fmtFloat2(fbuf, sizeof(fbuf), m_freq_hz);
    fmtFloat3(mbuf, sizeof(mbuf), m_mod_idx);
    char msg[48];
    std::snprintf(msg, sizeof(msg), "[OL] START f=%s m=%s", fbuf, mbuf);
    Telemetry::log("print", msg);

    /* Start accumulating encoder mechanical cycles vs current zero crossings.
     * Pass the standstill raw sin/cos so the estimator can learn its DC
     * offsets without relying on the calibrated encoder angle. */
    PolePairEstimator::instance().setEnabled(
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

    /* Stop pole-pair estimation so coast-down noise/decay does not corrupt
     * the accumulated result.  The last estimate is preserved. */
    PolePairEstimator::instance().setEnabled(false);

    m_running = false;
    Telemetry::log("print", "[OL] STOPPED (coast)");
}

void OpenLoopController::setFrequency(float freq_hz) {
    if (freq_hz < 0.0f) freq_hz = 0.0f;
    m_freq_hz = freq_hz;

    PolePairEstimator::instance().setElectricalFrequency(m_freq_hz);

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
        Telemetry::log("print", "[OL] Critical fault detected - stopping");
        FaultManager::instance().printSummary();
        stop();
    }
}

} // namespace Inverter
