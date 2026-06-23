#include "Inverter/Control/OpenLoopController.h"

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

void OpenLoopController::applyModulation(float modulation_index) {
    if (modulation_index < 0.0f) modulation_index = 0.0f;
    if (modulation_index > 1.154700538f) modulation_index = 1.154700538f;
    m_mod_idx = modulation_index;
    PWM_SetSPWMParams(m_freq_hz, m_mod_idx);
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

    PolePairEstimator::instance().setElectricalFrequency(m_freq_hz);

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

    /* Start accumulating encoder mechanical cycles vs current zero crossings.
     * Pass the standstill raw sin/cos so the estimator can learn its DC
     * offsets without relying on the calibrated encoder angle. */
    PolePairEstimator::instance().setEnabled(
        true, encoderADC().lastRawSin(), encoderADC().lastRawCos());
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

    /* Stop pole-pair estimation so coast-down noise/decay does not corrupt
     * the accumulated result.  The last estimate is preserved. */
    PolePairEstimator::instance().setEnabled(false);

    m_running = false;
    m_cal_state = CalState::IDLE;
    m_cal_mod = 0.0f;
    Telemetry::log("print", "[OL] STOPPED");
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

bool OpenLoopController::startCalibration() {
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
        Telemetry::log("print", "[OL CAL] ERROR: gate driver not ready or fault");
        GateDriver_DisableOutputs();
        return false;
    }

    /* Fixed 1 Hz, start at zero modulation. */
    m_freq_hz = 1.0f;
    m_mod_idx = 0.0f;
    m_cal_mod = 0.0f;
    PWM_ResetSPWMElectricalCycles();
    PWM_StartSPWM(m_freq_hz, 0.0f);
    m_running = true;

    PolePairEstimator::instance().setElectricalFrequency(m_freq_hz);
    PolePairEstimator::instance().setEnabled(
        true, encoderADC().lastRawSin(), encoderADC().lastRawCos());

    m_cal_mech_start = PolePairEstimator::instance().mechanicalCycles();
    m_cal_last_mech = m_cal_mech_start;
    m_cal_last_ramp_ms = HAL_GetTick();
    m_cal_last_move_ms = HAL_GetTick();
    m_cal_state = CalState::RAMP;

    Telemetry::log("print", "[OL CAL] started at 1 Hz, ramping modulation until encoder moves");
    return true;
}

void OpenLoopController::reportCalibrationRatio(const char* label) {
    const float mech_counted = m_cal_last_mech - m_cal_mech_count_start;
    const uint32_t elec_counted = PWM_GetSPWMElectricalCycles() - m_cal_elec_count_start;
    const float ratio = (mech_counted > 0.0f)
                            ? static_cast<float>(elec_counted) / mech_counted
                            : 0.0f;

    char ratio_buf[16];
    fmtFloat3(ratio_buf, sizeof(ratio_buf), ratio);
    char mod_buf[16];
    fmtFloat3(mod_buf, sizeof(mod_buf), m_cal_mod);
    char mech_buf[16];
    fmtFloat2(mech_buf, sizeof(mech_buf), mech_counted);
    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "[OL CAL] %s: ratio=%s at mod=%s (elec=%lu mech=%s)",
                  label, ratio_buf, mod_buf,
                  static_cast<unsigned long>(elec_counted),
                  mech_buf);
    Telemetry::log("print", msg);
}

void OpenLoopController::runCalibration() {
    if (m_cal_state == CalState::IDLE) {
        return;
    }

    constexpr float CAL_MAX_MOD = 0.95f;        /* leave SVPWM overmod headroom */
    constexpr float START_COUNT_CYCLES = 0.25f; /* start once it turns a quarter cycle */
    constexpr float TARGET_MECH_CYCLES = 2.0f;  /* 2 encoder cycles is enough for an integer ratio */
    constexpr float MIN_PARTIAL_CYCLES = 1.0f;
    constexpr uint32_t STALL_TIMEOUT_MS = 3000U;
    constexpr uint32_t MAX_COUNT_MS = 120000U;

    const uint32_t now_ms = HAL_GetTick();
    const float mech_now = PolePairEstimator::instance().mechanicalCycles();

    if (m_cal_state == CalState::RAMP) {
        /* Ramp modulation up slowly. */
        if ((now_ms - m_cal_last_ramp_ms) >= 100U) {
            m_cal_last_ramp_ms = now_ms;
            m_cal_mod += 0.005f;
            if (m_cal_mod > CAL_MAX_MOD) {
                m_cal_mod = CAL_MAX_MOD;
            }
            applyModulation(m_cal_mod);
        }

        /* Track any net forward encoder movement so a vibrating/cogging motor
         * that hasn't completed a full cycle still resets the stall timer. */
        if ((mech_now - m_cal_last_mech) > 0.01f) {
            m_cal_last_mech = mech_now;
            m_cal_last_move_ms = now_ms;
        }

        /* Once it has turned a reliable fraction of a cycle, start counting. */
        if ((mech_now - m_cal_mech_start) >= START_COUNT_CYCLES) {
            /* Add torque margin so it can pull through sticky spots. */
            m_cal_mod += 0.10f;
            if (m_cal_mod > CAL_MAX_MOD) {
                m_cal_mod = CAL_MAX_MOD;
            }
            applyModulation(m_cal_mod);

            m_cal_mech_count_start = mech_now;
            m_cal_elec_count_start = PWM_GetSPWMElectricalCycles();
            m_cal_count_start_ms = now_ms;
            m_cal_last_move_ms = now_ms;
            m_cal_last_mech = mech_now;
            m_cal_state = CalState::COUNT;
            Telemetry::log("print", "[OL CAL] encoder moving, counting cycles");
        }

        /* Give up if we have ramped to the limit and the shaft still has not turned. */
        if (m_cal_mod >= CAL_MAX_MOD &&
            (now_ms - m_cal_last_move_ms) > STALL_TIMEOUT_MS &&
            (mech_now - m_cal_mech_start) < START_COUNT_CYCLES) {
            m_cal_state = CalState::FAIL;
            Telemetry::log("print", "[OL CAL] FAIL: encoder did not move");
            stop();
        }
    }
    else if (m_cal_state == CalState::COUNT) {
        /* Watch for stalls during the measurement. */
        if ((mech_now - m_cal_last_mech) > 0.01f) {
            m_cal_last_mech = mech_now;
            m_cal_last_move_ms = now_ms;
        }

        if ((now_ms - m_cal_last_move_ms) > STALL_TIMEOUT_MS) {
            /* If it moved enough before stalling, report a partial result. */
            if ((mech_now - m_cal_mech_count_start) >= MIN_PARTIAL_CYCLES) {
                m_cal_state = CalState::DONE;
                reportCalibrationRatio("partial");
            } else {
                m_cal_state = CalState::FAIL;
                Telemetry::log("print", "[OL CAL] FAIL: encoder stalled during count");
            }
            stop();
            return;
        }

        if ((now_ms - m_cal_count_start_ms) > MAX_COUNT_MS) {
            m_cal_state = CalState::FAIL;
            Telemetry::log("print", "[OL CAL] FAIL: count took too long");
            stop();
            return;
        }

        /* Count over 5 encoder sin/cos cycles to average out jitter. */
        if ((mech_now - m_cal_mech_count_start) >= TARGET_MECH_CYCLES) {
            m_cal_last_mech = mech_now;
            reportCalibrationRatio("done");
            m_cal_state = CalState::DONE;
            stop();
        }
    }
}

void OpenLoopController::update() {
    if (!m_running) {
        return;
    }

    if (GateDriver_IsFault()) {
        Telemetry::log("print", "[OL] FAULT detected - stopping");
        stop();
        return;
    }

    if (m_cal_state != CalState::IDLE) {
        runCalibration();
    }
}

} // namespace Inverter
