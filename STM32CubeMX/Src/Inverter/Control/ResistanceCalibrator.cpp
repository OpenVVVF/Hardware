#include "Inverter/Control/ResistanceCalibrator.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include <cstdio>
#include <cmath>

namespace Inverter {

namespace {

void fmtFloat3(char* buf, size_t cap, float v) {
    int whole = static_cast<int>(v);
    int frac = static_cast<int>((v - whole) * 1000.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%03d", whole, frac);
}

void fmtFloat2(char* buf, size_t cap, float v) {
    int whole = static_cast<int>(v);
    int frac = static_cast<int>((v - whole) * 100.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%02d", whole, frac);
}

void fmtFloat1(char* buf, size_t cap, float v) {
    int whole = static_cast<int>(v);
    int frac = static_cast<int>((v - whole) * 10.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%01d", whole, frac);
}

} // namespace

static ResistanceCalibrator s_instance;

ResistanceCalibrator& resistanceCalibrator() {
    return s_instance;
}

/* Pair topology: driven and measured phase is always a real U or V sensor.
 *   pair 0: U driven, V grounded, W high-Z  -> R_uv
 *   pair 1: U driven, W grounded, V high-Z  -> R_uw
 *   pair 2: V driven, W grounded, U high-Z  -> R_vw
 */
static constexpr uint8_t kDriven[3] = {0, 0, 1};
static constexpr uint8_t kGround[3] = {1, 2, 2};
static constexpr uint8_t kHighZ[3]  = {2, 1, 0};

/* Moderate voltage ramp.  There is no PI loop; the current is only a
 * threshold.  The ramp must be slow enough for the L/R dynamics and current
 * filter to follow, but fast enough that the test completes in a reasonable
 * time at the fixed 10 kHz PWM frequency. */
static constexpr float RAMP_RATE_PCT_PER_S = 15.0f; /**< % duty per second. */
static constexpr float CURRENT_WINDOW_A    = 0.5f;  /**< Threshold window. */

/* Encoder faults are irrelevant to a DC resistance measurement.  The DC current
 * can magnetically disturb the encoder or temporarily starve its sampling, so
 * we ignore them for the duration of the calibration. */
static constexpr uint32_t ENCODER_FAULT_MASK =
    static_cast<uint32_t>(FaultSource::EncoderDma) |
    static_cast<uint32_t>(FaultSource::EncoderAmplitude) |
    static_cast<uint32_t>(FaultSource::EncoderOutOfRange) |
    static_cast<uint32_t>(FaultSource::EncoderTimeout);

static bool hasCriticalOrHighNonEncoderFault() {
    const uint32_t flags = FaultManager::instance().activeFlags() & ~ENCODER_FAULT_MASK;
    if (flags == 0) {
        return false;
    }
    for (size_t i = 0; i < FaultManager::metaCount(); ++i) {
        const auto& m = FaultManager::metaTable()[i];
        if ((flags & static_cast<uint32_t>(m.source)) != 0 &&
            (m.severity == FaultSeverity::Critical || m.severity == FaultSeverity::High)) {
            return true;
        }
    }
    return false;
}

bool ResistanceCalibrator::start() {
    return start(Config{});
}

bool ResistanceCalibrator::start(const Config& cfg) {
    if (m_state != State::Idle && m_state != State::Done && m_state != State::Error) {
        Telemetry::log("print", "[RESCAL] already active");
        return false;
    }

    if (openLoopController().isRunning()) {
        Telemetry::log("print", "[RESCAL] stop the motor first");
        return false;
    }

    if (!GateDriver_IsReady()) {
        Telemetry::log("print", "[RESCAL] gate driver not ready");
        return false;
    }

    if (GateDriver_IsFault()) {
        Telemetry::log("print", "[RESCAL] gate driver fault active; clear fault first");
        return false;
    }

    if (hasCriticalOrHighNonEncoderFault()) {
        Telemetry::log("print", "[RESCAL] clear active faults first");
        return false;
    }

    if (cfg.max_current_a <= 0.0f || cfg.num_points == 0 || cfg.num_points > MAX_POINTS) {
        Telemetry::log("print", "[RESCAL] invalid config");
        return false;
    }

    m_cfg = cfg;
    m_result = {};
    for (auto& pd : m_pair_data) {
        pd = {};
    }

    /* The software overcurrent threshold must stay above the ramp abort limit
     * so a noisy single sample does not beat the calibrator to the shutdown. */
    m_saved_sw_oc = phaseCurrentADC().overcurrentThreshold();
    m_saved_hw_oc = phaseCurrentADC().hardwareOvercurrentThreshold();

    m_abort_limit = (cfg.max_current_a * 1.5f > 100.0f)
                        ? cfg.max_current_a * 1.5f
                        : 100.0f;
    m_sw_oc_limit = m_abort_limit + 50.0f;
    phaseCurrentADC().setOvercurrentThreshold(m_sw_oc_limit);

    if (m_saved_hw_oc > 0.0f && m_saved_hw_oc < m_sw_oc_limit) {
        Telemetry::log("print", "[RESCAL] WARNING: hardware overcurrent threshold is lower than cal limit");
    }

    enterState(State::Init);
    return true;
}

void ResistanceCalibrator::abort() {
    if (m_state == State::Idle) {
        return;
    }
    cleanup(true);
    std::snprintf(m_result.message, sizeof(m_result.message), "aborted");
}

void ResistanceCalibrator::service() {
    const uint32_t now = HAL_GetTick();

    /* If a critical or high fault occurs during calibration, abort immediately.
     * Encoder faults are ignored because the DC current can disturb the
     * encoder without affecting the resistance measurement. */
    if (m_state != State::Idle && m_state != State::Done && m_state != State::Error) {
        if (hasCriticalOrHighNonEncoderFault()) {
            cleanup(true);
            std::snprintf(m_result.message, sizeof(m_result.message), "fault during calibration");
            return;
        }
    }

    switch (m_state) {
        case State::Idle:
        case State::Done:
        case State::Error:
            break;

        case State::Init:
            doInit();
            break;

        case State::CurrentZeroDelay:
            doCurrentZeroDelay();
            break;

        case State::SetupPair:
            doSetupPair();
            break;

        case State::Ramp:
            doRamp();
            break;

        case State::Settle:
            doSettle();
            break;

        case State::Sample:
            doSample();
            break;

        case State::InterPairDelay:
            doInterPairDelay();
            break;

        case State::Compute:
            doCompute();
            break;
    }

    m_last_update_ms = now;
}

bool ResistanceCalibrator::isActive() const {
    return m_state != State::Idle && m_state != State::Done && m_state != State::Error;
}

const char* ResistanceCalibrator::stateName() const {
    switch (m_state) {
        case State::Idle:           return "idle";
        case State::Init:           return "init";
        case State::CurrentZeroDelay: return "zerocur";
        case State::SetupPair:      return "setup";
        case State::Ramp:           return "ramp";
        case State::Settle:         return "settle";
        case State::Sample:         return "sample";
        case State::InterPairDelay: return "delay";
        case State::Compute:        return "compute";
        case State::Done:           return "done";
        case State::Error:          return "error";
    }
    return "?";
}

void ResistanceCalibrator::enterState(State s) {
    m_state = s;
    m_state_enter_ms = HAL_GetTick();
}

void ResistanceCalibrator::doInit() {
    /* Stop any modulation and put all three phases in high-Z before enabling
     * the gate driver.  This prevents a spinning motor from seeing a zero
     * vector (which shorts the windings) and causing an immediate current
     * spike when the outputs are enabled. */
    PWM_StopSPWM();
    for (uint8_t p = 0; p < 3; ++p) {
        PWM_StopPhase(p);
    }

    /* Keep the existing PWM frequency.  Changing it on the fly shifts the ADC
     * sampling point and leaves phase-current telemetry noisy until reboot. */

    /* Park all phases at 50 % while the channels are disabled. */
    PWM_ClearFault();
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);

    /* Make sure the gate driver is cleanly woken up.  Assert reset while the
     * power rail stabilizes, then release it.  This clears any latched DESAT
     * event from a previous run. */
    GateDriver_EnablePower(true);
    HAL_Delay(20);
    GateDriver_DisableOutputs();
    HAL_Delay(10);
    GateDriver_EnableOutputs();

    /* Wait for the gate-driver /RDY and /FLT lines.  Outputs are disabled until
     * /RDY is high (pin HIGH) and /FLT is high (no fault). */
    const uint32_t ready_wait_start = HAL_GetTick();
    while (!GateDriver_IsReady()) {
        if ((HAL_GetTick() - ready_wait_start) > 100U) {
            cleanup(true);
            std::snprintf(m_result.message, sizeof(m_result.message),
                          "gate driver not ready");
            Telemetry::log("print", "[RESCAL] ERROR: gate driver /RDY stays low");
            return;
        }
    }

    const uint32_t fault_wait_start = HAL_GetTick();
    while (GateDriver_IsFault()) {
        if ((HAL_GetTick() - fault_wait_start) > 100U) {
            cleanup(true);
            std::snprintf(m_result.message, sizeof(m_result.message),
                          "gate driver fault persists");
            Telemetry::log("print", "[RESCAL] ERROR: gate driver /FLT stays low");
            return;
        }
    }

    /* Make sure TIM1 MOE actually takes.  A break input that is still asserted
     * will re-disable MOE immediately, so retry a few times and log the result. */
    bool moe_ok = false;
    for (int i = 0; i < 10; ++i) {
        PWM_ClearFault();
        if ((TIM1->BDTR & TIM_BDTR_MOE) != 0U) {
            moe_ok = true;
            break;
        }
        HAL_Delay(1);
    }

    char diag[96];
    std::snprintf(diag, sizeof(diag),
                  "[RESCAL] rdY=%s flt=%s moe=%s",
                  GateDriver_IsReady() ? "Y" : "N",
                  GateDriver_IsFault() ? "Y" : "N",
                  moe_ok ? "Y" : "N");
    Telemetry::log("print", diag);

    if (!moe_ok) {
        cleanup(true);
        std::snprintf(m_result.message, sizeof(m_result.message),
                      "TIM1 MOE will not set");
        Telemetry::log("print", "[RESCAL] ERROR: TIM1 MOE disabled");
        return;
    }

    m_pair_index = 0;
    m_point_index = 0;
    m_duty = 0.0f;
    m_i_filt = 0.0f;
    m_i_filt_init = false;

    enterState(State::CurrentZeroDelay);
}

void ResistanceCalibrator::doCurrentZeroDelay() {
    const uint32_t now = HAL_GetTick();
    if (now - m_state_enter_ms >= 300U) {
        float iu = 0.0f, iv = 0.0f, iw = 0.0f;
        (void)phaseCurrentADC().sample(iu, iv, iw);
        const float imax = std::max(std::fabs(iu), std::max(std::fabs(iv), std::fabs(iw)));
        if (imax > 2.0f) {
            cleanup(true);
            std::snprintf(m_result.message, sizeof(m_result.message),
                          "motor current not zero");
            Telemetry::log("print", "[RESCAL] ERROR: motor current not zero; stop the shaft");
            return;
        }
        enterState(State::SetupPair);
    }
}

void ResistanceCalibrator::doSetupPair() {
    m_pair_data[m_pair_index].count = 0;
    m_point_index = 0;
    m_target_current = 0.0f;
    m_duty = 0.0f;
    m_settle_start_ms = 0;
    m_point_start_ms = HAL_GetTick();
    m_samples = 0;
    m_i_filt = 0.0f;
    m_i_filt_init = false;

    setPwmForPair();

    char msg[64];
    std::snprintf(msg, sizeof(msg), "[RESCAL] pair %lu",
                  static_cast<unsigned long>(m_pair_index));
    Telemetry::log("print", msg);

    enterState(State::Ramp);
}

void ResistanceCalibrator::doRamp() {
    if (m_point_index >= m_cfg.num_points) {
        enterState(State::InterPairDelay);
        return;
    }

    const uint32_t now = HAL_GetTick();
    const float target = m_cfg.max_current_a *
                         static_cast<float>(m_point_index + 1U) /
                         static_cast<float>(m_cfg.num_points);

    /* Ramp the duty cycle at a fixed, slow rate. */
    float dt = (m_last_update_ms == 0)
                   ? 0.001f
                   : static_cast<float>(now - m_last_update_ms) / 1000.0f;
    if (dt < 0.0f) dt = 0.001f;
    if (dt > 0.05f) dt = 0.05f;

    m_duty += RAMP_RATE_PCT_PER_S * dt;
    if (m_duty > m_cfg.duty_max) {
        m_duty = m_cfg.duty_max;
    }
    if (m_duty < m_cfg.duty_min) {
        m_duty = m_cfg.duty_min;
    }
    PWM_SetDutyCycle(kDriven[m_pair_index], m_duty);

    const float i = filteredMeasuredCurrentForPair();

    /* Abort if the current exceeds the intended headroom.  The software OC
     * threshold is set 50 A higher so a single noisy sample does not trip
     * before this check runs. */
    if (i > m_abort_limit) {
        cleanup(true);
        char ia[16];
        fmtFloat2(ia, sizeof(ia), i);
        std::snprintf(m_result.message, sizeof(m_result.message),
                      "overcurrent %s A", ia);
        Telemetry::log("print", "[RESCAL] ERROR: overcurrent");
        return;
    }

    /* Threshold reached, or we have run out of duty range. */
    const bool reached = (i >= (target - CURRENT_WINDOW_A)) ||
                         (m_duty >= m_cfg.duty_max);

    /* Once-per-second progress log so the user can see the ramp moving. */
    static uint32_t s_last_ramp_log_ms = 0;
    if (now - s_last_ramp_log_ms >= 1000U) {
        s_last_ramp_log_ms = now;
        char it[16], ii[16], id[16];
        fmtFloat2(it, sizeof(it), target);
        fmtFloat2(ii, sizeof(ii), i);
        fmtFloat1(id, sizeof(id), m_duty);
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "[RESCAL] ramp p%lu pt%lu target=%s A i=%s A d=%s %%",
                      static_cast<unsigned long>(m_pair_index),
                      static_cast<unsigned long>(m_point_index),
                      it, ii, id);
        Telemetry::log("print", msg);
    }

    if (reached) {
        m_target_current = target;
        enterState(State::Settle);
        return;
    }

    if (now - m_point_start_ms >= m_cfg.timeout_ms) {
        cleanup(true);
        char msg[80];
        char itarget[16];
        fmtFloat2(itarget, sizeof(itarget), target);
        std::snprintf(msg, sizeof(msg),
                      "[RESCAL] timeout at pair %lu point %lu (I=%s A)",
                      static_cast<unsigned long>(m_pair_index),
                      static_cast<unsigned long>(m_point_index),
                      itarget);
        Telemetry::log("print", msg);
        std::snprintf(m_result.message, sizeof(m_result.message),
                      "timeout pair %lu point %lu",
                      static_cast<unsigned long>(m_pair_index),
                      static_cast<unsigned long>(m_point_index));
    }
}

void ResistanceCalibrator::doSettle() {
    const uint32_t now = HAL_GetTick();
    const float target = m_cfg.max_current_a *
                         static_cast<float>(m_point_index + 1U) /
                         static_cast<float>(m_cfg.num_points);
    const float i = filteredMeasuredCurrentForPair();

    /* Hold the voltage that crossed the threshold. */
    PWM_SetDutyCycle(kDriven[m_pair_index], m_duty);

    if (i > m_abort_limit) {
        cleanup(true);
        std::snprintf(m_result.message, sizeof(m_result.message), "overcurrent");
        Telemetry::log("print", "[RESCAL] ERROR: overcurrent during settle");
        return;
    }

    /* If the current fell well below the window we can still try to ramp a
     * little higher, unless we are already at the duty limit.  Use a lower
     * threshold than the entry threshold so noise around the window does not
     * cause Settle/Ramp chatter that resets the per-point timeout. */
    if (i < (target - 2.0f * CURRENT_WINDOW_A) && m_duty < m_cfg.duty_max) {
        enterState(State::Ramp);
        return;
    }

    if (m_settle_start_ms == 0) {
        m_settle_start_ms = now;
    }

    if (now - m_settle_start_ms >= m_cfg.settle_ms) {
        m_settle_start_ms = 0;
        enterState(State::Sample);
    }
}

void ResistanceCalibrator::doSample() {
    const uint32_t now = HAL_GetTick();

    if (m_samples == 0) {
        m_i_sum = 0.0f;
        m_v_sum = 0.0f;
    }

    /* Hold the voltage constant while averaging. */
    PWM_SetDutyCycle(kDriven[m_pair_index], m_duty);

    const float vdc = dcLinkVoltageSensor().voltage();
    const float i = filteredMeasuredCurrentForPair();
    m_i_sum += i;
    m_v_sum += vdc * m_duty / 100.0f;
    ++m_samples;

    if (now - m_state_enter_ms >= m_cfg.sample_window_ms) {
        PairData& pd = m_pair_data[m_pair_index];
        if (pd.count < MAX_POINTS) {
            pd.i[pd.count] = m_i_sum / static_cast<float>(m_samples);
            pd.v[pd.count] = m_v_sum / static_cast<float>(m_samples);
            ++pd.count;
        }

        char ai[16], av[16], ad[16];
        fmtFloat2(ai, sizeof(ai), pd.i[pd.count - 1]);
        fmtFloat2(av, sizeof(av), pd.v[pd.count - 1]);
        fmtFloat1(ad, sizeof(ad), m_duty);
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "[RESCAL]  point %lu/%lu: I=%s A V=%s V D=%s %%",
                      static_cast<unsigned long>(m_point_index + 1),
                      static_cast<unsigned long>(m_cfg.num_points),
                      ai, av, ad);
        Telemetry::log("print", msg);

        m_samples = 0;
        ++m_point_index;
        m_settle_start_ms = 0;
        m_point_start_ms = now;
        enterState(State::Ramp);
    }
}

void ResistanceCalibrator::doInterPairDelay() {
    /* Coast all phases and let the current decay before reconfiguring. */
    for (uint8_t p = 0; p < 3; ++p) {
        PWM_SetDutyCycle(p, 0.0f);
        PWM_StopPhase(p);
    }

    const uint32_t now = HAL_GetTick();
    if (now - m_state_enter_ms >= 300U) {
        ++m_pair_index;
        if (m_pair_index >= 3) {
            enterState(State::Compute);
        } else {
            enterState(State::SetupPair);
        }
    }
}

void ResistanceCalibrator::doCompute() {
    bool ok = true;
    float r_ll[3] = {0.0f, 0.0f, 0.0f};

    for (int p = 0; p < 3; ++p) {
        const PairData& pd = m_pair_data[p];
        if (pd.count < 2) {
            ok = false;
            break;
        }
        r_ll[p] = linearRegressionSlope(pd.v, pd.i, pd.count);
        if (r_ll[p] <= 0.0f || !std::isfinite(r_ll[p])) {
            ok = false;
            break;
        }
    }

    if (!ok) {
        cleanup(true);
        std::snprintf(m_result.message, sizeof(m_result.message), "insufficient data");
        Telemetry::log("print", "[RESCAL] ERROR: insufficient data");
        return;
    }

    m_result.r_uv = r_ll[0];
    m_result.r_uw = r_ll[1];
    m_result.r_vw = r_ll[2];

    /* Solve wye network for individual phase resistances. */
    m_result.r_u = 0.5f * (r_ll[0] + r_ll[1] - r_ll[2]);
    m_result.r_v = 0.5f * (r_ll[0] + r_ll[2] - r_ll[1]);
    m_result.r_w = 0.5f * (r_ll[1] + r_ll[2] - r_ll[0]);

    if (m_result.r_u <= 0.0f || m_result.r_v <= 0.0f || m_result.r_w <= 0.0f) {
        cleanup(true);
        std::snprintf(m_result.message, sizeof(m_result.message), "invalid resistance");
        Telemetry::log("print", "[RESCAL] ERROR: invalid resistance (delta?)");
        return;
    }

    /* Check triangle inequality on the raw line-to-line values as well; this
     * catches inconsistent pair measurements before they produce a negative
     * wye resistance. */
    const float min_ll = std::min(r_ll[0], std::min(r_ll[1], r_ll[2]));
    const float max_ll = std::max(r_ll[0], std::max(r_ll[1], r_ll[2]));
    if ((r_ll[0] + r_ll[1] <= r_ll[2]) ||
        (r_ll[0] + r_ll[2] <= r_ll[1]) ||
        (r_ll[1] + r_ll[2] <= r_ll[0]) ||
        (max_ll > 3.0f * min_ll)) {
        cleanup(true);
        std::snprintf(m_result.message, sizeof(m_result.message), "inconsistent pairs");
        Telemetry::log("print", "[RESCAL] ERROR: inconsistent pair measurements");
        return;
    }

    m_result.r_avg = (m_result.r_u + m_result.r_v + m_result.r_w) / 3.0f;

    const float dev_u = std::fabs(m_result.r_u - m_result.r_avg);
    const float dev_v = std::fabs(m_result.r_v - m_result.r_avg);
    const float dev_w = std::fabs(m_result.r_w - m_result.r_avg);
    const float max_dev = std::max(dev_u, std::max(dev_v, dev_w));
    m_result.imbalance_pct = (m_result.r_avg > 0.0f) ? (max_dev / m_result.r_avg) : 0.0f;
    m_result.imbalance = m_result.imbalance_pct > m_cfg.imbalance_tol;
    m_result.valid = true;

    cleanup(false);

    char ru[16], rv[16], rw[16], ravg[16];
    fmtFloat3(ru, sizeof(ru), m_result.r_u);
    fmtFloat3(rv, sizeof(rv), m_result.r_v);
    fmtFloat3(rw, sizeof(rw), m_result.r_w);
    fmtFloat3(ravg, sizeof(ravg), m_result.r_avg);

    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "[RESCAL] R_u=%s R_v=%s R_w=%s R_avg=%s Ohm",
                  ru, rv, rw, ravg);
    Telemetry::log("print", msg);

    if (m_result.imbalance) {
        char imb[16];
        fmtFloat1(imb, sizeof(imb), m_result.imbalance_pct * 100.0f);
        std::snprintf(msg, sizeof(msg),
                      "[RESCAL] WARNING: phase imbalance %s %% exceeds tolerance",
                      imb);
        Telemetry::log("print", msg);
        std::snprintf(m_result.message, sizeof(m_result.message),
                      "imbalance %s %%", imb);
    } else {
        std::snprintf(m_result.message, sizeof(m_result.message), "ok");
    }
}

void ResistanceCalibrator::cleanup(bool error) {
    /* Coast outputs and restore safe defaults. */
    PWM_StopSPWM();
    for (uint8_t p = 0; p < 3; ++p) {
        PWM_StopPhase(p);
    }
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
    restorePwm();
    GateDriver_DisableOutputs();

    phaseCurrentADC().setOvercurrentThreshold(m_saved_sw_oc);

    /* Recalibrate current-sensor offsets after the DC excitation.  This usually
     * fixes any zero drift caused by the calibration current. */
    PhaseCurrentADC& cur_adc = phaseCurrentADC();
    const float offset_u_before = cur_adc.lastOffsetU();
    const float offset_v_before = cur_adc.lastOffsetV();
    const bool recal_ok = cur_adc.recalibrateOffsets();

    char u_before[16], v_before[16], u_after[16], v_after[16];
    fmtFloat3(u_before, sizeof(u_before), offset_u_before);
    fmtFloat3(v_before, sizeof(v_before), offset_v_before);
    fmtFloat3(u_after, sizeof(u_after), cur_adc.lastOffsetU());
    fmtFloat3(v_after, sizeof(v_after), cur_adc.lastOffsetV());
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "[RESCAL] offsets before U=%s V=%s after U=%s V=%s (%s)",
                  u_before, v_before, u_after, v_after,
                  recal_ok ? "ok" : "FAILED");
    Telemetry::log("print", msg);

    /* Drop any encoder faults that were raised only because the calibration
     * current disturbed the encoder or its sampling. */
    FaultManager::instance().clear(FaultSource::EncoderDma);
    FaultManager::instance().clear(FaultSource::EncoderAmplitude);
    FaultManager::instance().clear(FaultSource::EncoderOutOfRange);
    FaultManager::instance().clear(FaultSource::EncoderTimeout);

    m_pair_index = 0;
    m_point_index = 0;
    m_target_current = 0.0f;
    m_duty = 0.0f;
    m_i_filt = 0.0f;
    m_i_filt_init = false;
    m_samples = 0;
    m_last_update_ms = 0;
    m_settle_start_ms = 0;
    m_point_start_ms = 0;

    enterState(error ? State::Error : State::Done);
}

float ResistanceCalibrator::rawMeasuredCurrentForPair() const {
    const uint8_t phase = kDriven[m_pair_index];
    float iu = 0.0f, iv = 0.0f, iw = 0.0f;
    (void)phaseCurrentADC().sample(iu, iv, iw);
    /* Only the real U or V sensor is ever used. */
    if (phase == 0) {
        return iu;
    } else if (phase == 1) {
        return iv;
    }
    return 0.0f;
}

float ResistanceCalibrator::filteredMeasuredCurrentForPair() {
    const float raw = std::fabs(rawMeasuredCurrentForPair());
    if (!m_i_filt_init) {
        m_i_filt = raw;
        m_i_filt_init = true;
        return raw;
    }
    /* Gentle IIR filter: ~10 sample time-constant at 1 kHz service rate. */
    constexpr float ALPHA = 0.1f;
    m_i_filt += ALPHA * (raw - m_i_filt);
    return m_i_filt;
}

void ResistanceCalibrator::setPwmForPair() {
    /* Driven phase starts at 0 %, grounded phase at 0 % (low side on), high-Z
     * phase disabled.  The driven-phase duty will be ramped upward. */
    PWM_SetDutyCycle(kDriven[m_pair_index], 0.0f);
    PWM_SetDutyCycle(kGround[m_pair_index], 0.0f);
    PWM_SetDutyCycle(kHighZ[m_pair_index], 0.0f);

    for (uint8_t p = 0; p < 3; ++p) {
        PWM_StartPhase(p);
    }
    PWM_StopPhase(kHighZ[m_pair_index]);
}

void ResistanceCalibrator::restorePwm() {
    /* Re-enable all three complementary channels for normal operation. */
    for (uint8_t p = 0; p < 3; ++p) {
        PWM_StartPhase(p);
    }
}

float ResistanceCalibrator::linearRegressionSlope(const float v[], const float i[], uint32_t n) {
    if (n < 2) return 0.0f;
    float sum_v = 0.0f, sum_i = 0.0f, sum_ii = 0.0f, sum_vi = 0.0f;
    for (uint32_t k = 0; k < n; ++k) {
        sum_v  += v[k];
        sum_i  += i[k];
        sum_ii += i[k] * i[k];
        sum_vi += v[k] * i[k];
    }
    const float denom = n * sum_ii - sum_i * sum_i;
    if (std::fabs(denom) < 1e-12f) return 0.0f;
    return (n * sum_vi - sum_v * sum_i) / denom;
}

} // namespace Inverter
