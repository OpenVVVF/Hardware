#include "Inverter/Control/OpenLoopController.h"

#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
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

static OpenLoopController s_instance;

void OpenLoopController::rampModulation(float from_m, float to_m, uint32_t ramp_ms) {
    const int steps = 20;
    const int step_ms = static_cast<int>(ramp_ms / steps);
    if (step_ms <= 0) {
        PWM_SetSPWMParams(m_freq_hz, to_m);
        return;
    }

    for (int i = 1; i <= steps; ++i) {
        if (GateDriver_IsFault()) {
            PWM_StopSPWM();
            GateDriver_DisableOutputs();
            m_running = false;
            Telemetry::log("print", "[OL] FAULT during ramp - stopped");
            return;
        }
        float m = from_m + (to_m - from_m) * static_cast<float>(i) / static_cast<float>(steps);
        PWM_SetSPWMParams(m_freq_hz, m);
        HAL_Delay(step_ms);
    }
}

OpenLoopController& openLoopController() {
    return s_instance;
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
        return false;
    }

    m_freq_hz = (freq_hz < 0.0f) ? 0.0f : freq_hz;
    m_mod_idx = (modulation_index < 0.0f) ? 0.0f : modulation_index;

    /* Start the angle ramp at zero modulation, then ramp voltage up. */
    PWM_StartSPWM(m_freq_hz, 0.0f);
    m_running = true;
    rampModulation(0.0f, m_mod_idx, 100U);

    char fbuf[16], mbuf[16];
    fmtFloat2(fbuf, sizeof(fbuf), m_freq_hz);
    fmtFloat3(mbuf, sizeof(mbuf), m_mod_idx);
    char msg[48];
    std::snprintf(msg, sizeof(msg), "[OL] START f=%s m=%s", fbuf, mbuf);
    Telemetry::log("print", msg);
    return true;
}

void OpenLoopController::stop() {
    if (m_running) {
        /* Ramp the modulation index down to zero before turning the outputs
         * off. This lets the motor current decay smoothly and avoids the
         * audible hitch from an abrupt open-circuit or zero-vector step. */
        rampModulation(m_mod_idx, 0.0f, 100U);
    }

    PWM_StopSPWM();

    /* Park at 50 % (zero vector). TIM1 keeps running for current sense. */
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);

    /* Now that current has decayed, disable the gate-driver outputs. */
    GateDriver_DisableOutputs();

    /* Recalibrate current-sensor offsets while the motor is guaranteed
     * to be at zero current. This removes any drift accumulated during
     * the run and avoids the need for a manual `cal` after each stop. */
    (void)phaseCurrentADC().recalibrateOffsets();

    m_running = false;
    Telemetry::log("print", "[OL] STOPPED");
}

void OpenLoopController::setFrequency(float freq_hz) {
    if (freq_hz < 0.0f) freq_hz = 0.0f;
    m_freq_hz = freq_hz;

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

void OpenLoopController::update() {
    if (!m_running) {
        return;
    }

    if (GateDriver_IsFault()) {
        Telemetry::log("print", "[OL] FAULT detected - stopping");
        stop();
    }
}

} // namespace Inverter
