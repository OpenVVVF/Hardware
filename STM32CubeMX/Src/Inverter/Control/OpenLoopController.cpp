#include "Inverter/Control/OpenLoopController.h"

#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
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

    /* Start TIM1 so the ADC current-sense trigger keeps running, but keep
     * the gate driver in reset so the power stage stays off. */
    PWM_ClearFault();
    PWM_Start();

    /* Explicitly enable the gate-driver power rail (active high). */
    HAL_GPIO_WritePin(GATE_DRIVER_POWER_ENABLE_GPIO_Port, GATE_DRIVER_POWER_ENABLE_Pin, GPIO_PIN_SET);
    HAL_Delay(100);

    GateDriver_Init();

    /* Hold the gate driver in reset (active low) so its outputs are disabled
     * until a start command is issued. */
    GateDriver_DisableOutputs();

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

    PWM_StartSPWM(m_freq_hz, m_mod_idx);
    m_running = true;

    char fbuf[16], mbuf[16];
    fmtFloat2(fbuf, sizeof(fbuf), m_freq_hz);
    fmtFloat3(mbuf, sizeof(mbuf), m_mod_idx);
    char msg[48];
    std::snprintf(msg, sizeof(msg), "[OL] START f=%s m=%s", fbuf, mbuf);
    Telemetry::log("print", msg);
    return true;
}

void OpenLoopController::stop() {
    PWM_StopSPWM();

    /* Park at 50 % (zero vector) and disable the gate-driver outputs.
     * TIM1 keeps running so current-sense triggering stays alive. */
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
    GateDriver_DisableOutputs();

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
    m_mod_idx = modulation_index;

    if (m_running) {
        PWM_SetSPWMParams(m_freq_hz, m_mod_idx);
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
