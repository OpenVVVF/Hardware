#include "Inverter/Calibration/ResistanceCalibrator.h"

#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "tim.h"
#include <cstdio>
#include <cmath>
#include <cstring>

namespace Inverter {

static ResistanceCalibrator s_instance;

ResistanceCalibrator& ResistanceCalibrator::instance() {
    return s_instance;
}

ResistanceCalibrator& resistanceCalibrator() {
    return s_instance;
}

namespace {

void fmtFloat2(char* buf, size_t cap, float v) {
    int whole = static_cast<int>(v);
    int frac = static_cast<int>((v - whole) * 100.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%02d", whole, frac);
}

void fmtFloat3(char* buf, size_t cap, float v) {
    int whole = static_cast<int>(v);
    int frac = static_cast<int>((v - whole) * 1000.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%03d", whole, frac);
}

void fmtFloat4(char* buf, size_t cap, float v) {
    int whole = static_cast<int>(v);
    int frac = static_cast<int>((v - whole) * 10000.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%04d", whole, frac);
}

float pairCurrentActive(float iu, float iv, float iw, ResistanceCalibrator::Pair pair) {
    (void)iw;
    /* The "active" phase is the high-side PWM phase.  Current physically flows
     * OUT of that phase (into the low phase), so the measured current into the
     * phase is negative.  Negate it so active current is positive. */
    switch (pair) {
        case ResistanceCalibrator::Pair::UV:
        case ResistanceCalibrator::Pair::UW:
            return -iu;
        case ResistanceCalibrator::Pair::VW:
            return -iv;
    }
    return 0.0f;
}

float pairCurrentInactive(float iu, float iv, float iw, ResistanceCalibrator::Pair pair) {
    switch (pair) {
        case ResistanceCalibrator::Pair::UV:
            return iw;
        case ResistanceCalibrator::Pair::UW:
            return iv;
        case ResistanceCalibrator::Pair::VW:
            return iu;
    }
    return 0.0f;
}

uint32_t pinNumber(uint32_t pin_mask) {
    return static_cast<uint32_t>(__builtin_ctz(pin_mask));
}

/* Set a phase pin to either alternate function or GPIO output with a
 * specified level. */
void setPin(uint32_t pin, bool alternate_function, bool output_high) {
    const uint32_t num = pinNumber(pin);
    const uint32_t mask = 3U << (num * 2U);

    uint32_t moder = GPIOE->MODER;
    moder &= ~mask;
    moder |= (alternate_function ? 2U : 1U) << (num * 2U);
    GPIOE->MODER = moder;

    if (!alternate_function) {
        if (output_high) {
            GPIOE->BSRR = pin;
        } else {
            GPIOE->BSRR = pin << 16U;
        }
    }
}

} // namespace

const char* ResistanceCalibrator::pairName(Pair pair) {
    switch (pair) {
        case Pair::UV: return "UV";
        case Pair::UW: return "UW";
        case Pair::VW: return "VW";
    }
    return "?";
}

int ResistanceCalibrator::pairIndex(Pair pair) {
    switch (pair) {
        case Pair::UV: return 0;
        case Pair::UW: return 1;
        case Pair::VW: return 2;
    }
    return 0;
}

const char* ResistanceCalibrator::directedPairName(DirectedPair dp) {
    switch (dp) {
        case DirectedPair::UV: return "U->V";
        case DirectedPair::VU: return "V->U";
        case DirectedPair::UW: return "U->W";
        case DirectedPair::WU: return "W->U";
        case DirectedPair::VW: return "V->W";
        case DirectedPair::WV: return "W->V";
    }
    return "?";
}

const char* ResistanceCalibrator::doublePairName(DoublePair dp) {
    switch (dp) {
        case DoublePair::U_VW: return "U->VW";
        case DoublePair::V_UW: return "V->UW";
        case DoublePair::W_UV: return "W->UV";
    }
    return "?";
}

float ResistanceCalibrator::lastResult(Pair pair) const {
    return m_results[pairIndex(pair)];
}

float ResistanceCalibrator::activeCurrentForDirectedPair(float iu, float iv, float iw,
                                                         DirectedPair dp) const {
    /* Active current is defined as positive current flowing from the
     * high-side phase to the low-side phase. */
    switch (dp) {
        case DirectedPair::UV: return -iu; /* current leaves U */
        case DirectedPair::VU: return -iv; /* current leaves V */
        case DirectedPair::UW: return -iu; /* current leaves U */
        case DirectedPair::WU: return -iw; /* current leaves W */
        case DirectedPair::VW: return -iv; /* current leaves V */
        case DirectedPair::WV: return -iw; /* current leaves W */
    }
    return 0.0f;
}

float ResistanceCalibrator::inactiveCurrentForDirectedPair(float iu, float iv, float iw,
                                                           DirectedPair dp) const {
    /* Inactive (high-Z) phase current for the single-pair symmetry check. */
    switch (dp) {
        case DirectedPair::UV: return iw;
        case DirectedPair::VU: return iw;
        case DirectedPair::UW: return iv;
        case DirectedPair::WU: return iv;
        case DirectedPair::VW: return iu;
        case DirectedPair::WV: return iu;
    }
    return 0.0f;
}

bool ResistanceCalibrator::symmetryOkForDoublePair(float iu, float iv, float iw,
                                                   DoublePair dp, float i_set) const {
    float ret1 = 0.0f;
    float ret2 = 0.0f;
    switch (dp) {
        case DoublePair::U_VW: ret1 = iv; ret2 = iw; break;
        case DoublePair::V_UW: ret1 = iu; ret2 = iw; break;
        case DoublePair::W_UV: ret1 = iu; ret2 = iv; break;
    }

    /* Both return currents should be non-zero and have the same sign. */
    if (std::fabs(ret1) < 0.1f || std::fabs(ret2) < 0.1f) {
        return false;
    }
    if ((ret1 > 0.0f) != (ret2 > 0.0f)) {
        return false;
    }

    /* Magnitudes should be approximately equal (each ~ i_set/2). */
    const float ratio = ret2 / ret1;
    if (std::fabs(ratio - 1.0f) > 0.1f) {
        return false;
    }

    /* KCL sanity check: active current (leaving high phase) should equal
     * sum of return currents (entering low phases). */
    float active = 0.0f;
    switch (dp) {
        case DoublePair::U_VW: active = -iu; break;
        case DoublePair::V_UW: active = -iv; break;
        case DoublePair::W_UV: active = -iw; break;
    }
    if (std::fabs(active - (ret1 + ret2)) > 0.15f * std::fabs(i_set)) {
        return false;
    }

    return true;
}

bool ResistanceCalibrator::start(float bus_pct, Pair pair, bool run_all,
                                 uint32_t timeout_ms, float max_current_a) {
    if (isActive()) {
        Telemetry::log("print", "[RES CAL] already running");
        return false;
    }

    if (openLoopController().isRunning()) {
        Telemetry::log("print", "[RES CAL] stop the motor before calibration");
        return false;
    }

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::log("print", "[RES CAL] active faults, cannot start");
        return false;
    }

    if (bus_pct < 0.0f) bus_pct = 0.0f;
    if (bus_pct > MAX_BUS_PCT) {
        char buf[16];
        fmtFloat2(buf, sizeof(buf), MAX_BUS_PCT);
        char msg[64];
        std::snprintf(msg, sizeof(msg), "[RES CAL] clamped bus_pct to %s %%", buf);
        Telemetry::log("print", msg);
        bus_pct = MAX_BUS_PCT;
    }

    m_mode = Mode::VOLTAGE_STEP;
    /* Evenly spaced duty points from bus_pct/NUM_POINTS up to bus_pct. */
    for (uint8_t i = 0; i < NUM_POINTS; ++i) {
        m_targets[i] = bus_pct * static_cast<float>(i + 1U) /
                       static_cast<float>(NUM_POINTS);
    }
    m_max_current_a = (max_current_a < 0.0f) ? 0.0f : max_current_a;
    m_timeout_ms = timeout_ms;
    m_pair_index = 0;
    m_point_index = 0;
    m_num_pairs = run_all ? 3U : 1U;
    m_pairs[0] = pair;
    if (run_all) {
        m_pairs[1] = (pair == Pair::UV) ? Pair::UW :
                     (pair == Pair::UW) ? Pair::VW : Pair::UV;
        m_pairs[2] = (pair == Pair::UV) ? Pair::VW :
                     (pair == Pair::UW) ? Pair::UV : Pair::UW;
    }

    m_results[0] = m_results[1] = m_results[2] = 0.0f;
    m_result_valid[0] = m_result_valid[1] = m_result_valid[2] = false;
    m_average_r_phase = 0.0f;
    for (uint8_t i = 0; i < NUM_POINTS; ++i) {
        resetMeasurementAccumulators(i);
    }
    m_pi_integral = 0.0f;
    m_pi_duty = 0.0f;
    m_pi_last_ms = 0;

    enterState(State::ENABLE);

    char buf[16];
    fmtFloat4(buf, sizeof(buf), bus_pct);
    char ibuf[16];
    fmtFloat3(ibuf, sizeof(ibuf), m_max_current_a);
    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] starting (%s) max %s %% bus, %u points, max I=%s A",
                  run_all ? "UV/UW/VW" : pairName(pair), buf,
                  static_cast<unsigned>(NUM_POINTS), ibuf);
    Telemetry::log("print", msg);
    return true;
}

bool ResistanceCalibrator::startCurrentCtrl(float max_current_a, Pair pair,
                                            bool run_all, uint32_t timeout_ms,
                                            float oc_limit_a) {
    if (isActive()) {
        Telemetry::log("print", "[RES CAL] already running");
        return false;
    }

    if (openLoopController().isRunning()) {
        Telemetry::log("print", "[RES CAL] stop the motor before calibration");
        return false;
    }

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::log("print", "[RES CAL] active faults, cannot start");
        return false;
    }

    if (max_current_a < 0.0f) max_current_a = 0.0f;
    if (oc_limit_a <= 0.0f) oc_limit_a = max_current_a * 1.2f;
    if (oc_limit_a < max_current_a) oc_limit_a = max_current_a;

    m_mode = Mode::CURRENT_CTRL;
    /* Exponentially spaced current setpoints from CURRENT_CTRL_MIN_A up to
     * max_current_a, concentrating points at the low-current IGBT knee. */
    if (max_current_a <= CURRENT_CTRL_MIN_A) {
        max_current_a = CURRENT_CTRL_MIN_A + 1.0f;
    }
    const float ratio = std::pow(max_current_a / CURRENT_CTRL_MIN_A,
                                 1.0f / static_cast<float>(NUM_POINTS));
    for (uint8_t i = 0; i < NUM_POINTS; ++i) {
        m_targets[i] = CURRENT_CTRL_MIN_A * std::pow(ratio, static_cast<float>(i + 1U));
    }
    m_max_current_a = oc_limit_a;
    if (oc_limit_a > 0.0f) {
        phaseCurrentADC().setOvercurrentThreshold(oc_limit_a);
    }
    m_timeout_ms = timeout_ms;
    m_pair_index = 0;
    m_point_index = 0;
    m_num_pairs = run_all ? 3U : 1U;
    m_pairs[0] = pair;
    if (run_all) {
        m_pairs[1] = (pair == Pair::UV) ? Pair::UW :
                     (pair == Pair::UW) ? Pair::VW : Pair::UV;
        m_pairs[2] = (pair == Pair::UV) ? Pair::VW :
                     (pair == Pair::UW) ? Pair::UV : Pair::UW;
    }

    m_results[0] = m_results[1] = m_results[2] = 0.0f;
    m_result_valid[0] = m_result_valid[1] = m_result_valid[2] = false;
    m_average_r_phase = 0.0f;
    for (uint8_t i = 0; i < NUM_POINTS; ++i) {
        resetMeasurementAccumulators(i);
    }
    m_pi_integral = 0.0f;
    m_pi_duty = PI_MIN_DUTY;
    m_pi_last_ms = 0;

    enterState(State::ENABLE);

    char buf[16];
    fmtFloat3(buf, sizeof(buf), max_current_a);
    char ibuf[16];
    fmtFloat3(ibuf, sizeof(ibuf), m_max_current_a);
    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] I-ctrl (%s) target %s A, %u points, oc=%s A",
                  run_all ? "UV/UW/VW" : pairName(pair), buf,
                  static_cast<unsigned>(NUM_POINTS), ibuf);
    Telemetry::log("print", msg);
    return true;
}

bool ResistanceCalibrator::startTopologyDiff(float max_current_a,
                                             uint32_t timeout_ms,
                                             float oc_limit_a) {
    if (isActive()) {
        Telemetry::log("print", "[RES CAL] already running");
        return false;
    }

    if (openLoopController().isRunning()) {
        Telemetry::log("print", "[RES CAL] stop the motor before calibration");
        return false;
    }

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::log("print", "[RES CAL] active faults, cannot start");
        return false;
    }

    if (max_current_a < 0.0f) max_current_a = 0.0f;
    if (oc_limit_a <= 0.0f) oc_limit_a = max_current_a * 1.2f;
    if (oc_limit_a < max_current_a) oc_limit_a = max_current_a;

    m_mode = Mode::TOPOLOGY_DIFF;
    if (max_current_a <= CURRENT_CTRL_MIN_A) {
        max_current_a = CURRENT_CTRL_MIN_A + 1.0f;
    }
    const float ratio = std::pow(max_current_a / CURRENT_CTRL_MIN_A,
                                 1.0f / static_cast<float>(TOPOLOGY_DIFF_POINTS));
    for (uint8_t i = 0; i < TOPOLOGY_DIFF_POINTS; ++i) {
        m_td_targets[i] = CURRENT_CTRL_MIN_A *
                          std::pow(ratio, static_cast<float>(i + 1U));
    }
    m_max_current_a = oc_limit_a;
    if (oc_limit_a > 0.0f) {
        phaseCurrentADC().setOvercurrentThreshold(oc_limit_a);
    }
    m_timeout_ms = timeout_ms;

    m_topology_point_index = 0;
    m_substep_index = 0;
    m_td_v1_sum = 0.0f;
    m_td_v2_sum = 0.0f;
    m_td_v1_count = 0;
    m_td_v2_count = 0;
    m_r_total = 0.0f;
    m_vce_lut_count = 0;
    for (uint8_t i = 0; i < TOPOLOGY_DIFF_POINTS; ++i) {
        m_td_i[i] = 0.0f;
        m_td_v1[i] = 0.0f;
        m_td_v2[i] = 0.0f;
        m_td_valid[i] = false;
        m_vce_lut_i[i] = 0.0f;
        m_vce_lut_v[i] = 0.0f;
    }
    resetSubstepAccumulators();

    m_pi_integral = 0.0f;
    m_pi_duty = PI_MIN_DUTY;
    m_pi_last_ms = 0;

    enterState(State::ENABLE);

    char buf[16];
    fmtFloat3(buf, sizeof(buf), max_current_a);
    char ibuf[16];
    fmtFloat3(ibuf, sizeof(ibuf), m_max_current_a);
    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] topodiff target %s A, %u points, oc=%s A",
                  buf, static_cast<unsigned>(TOPOLOGY_DIFF_POINTS), ibuf);
    Telemetry::log("print", msg);
    return true;
}

void ResistanceCalibrator::enterState(State state) {
    m_state = state;
    m_state_enter_ms = HAL_GetTick();
}

void ResistanceCalibrator::stop() {
    if (m_state != State::IDLE && m_state != State::DONE && m_state != State::FAIL) {
        Telemetry::log("print", "[RES CAL] stopped by user");
        restoreHardware();
        enterState(State::FAIL);
    }
}

void ResistanceCalibrator::fail(const char* reason) {
    restoreHardware();
    Telemetry::log("print", reason);
    enterState(State::FAIL);
}

void ResistanceCalibrator::resetMeasurementAccumulators(uint8_t point) {
    m_sample_count[point] = 0;
    m_sum_i_active[point] = 0.0f;
    m_sum_i_inactive[point] = 0.0f;
    m_sum_vdc[point] = 0.0f;
    m_sum_duty[point] = 0.0f;
}

void ResistanceCalibrator::resetSubstepAccumulators() {
    m_sub_sample_count = 0;
    m_sub_vdc_sum = 0.0f;
    m_sub_duty_sum = 0.0f;
    m_sub_i_active_sum = 0.0f;
    m_sub_i_ret1_sum = 0.0f;
    m_sub_i_ret2_sum = 0.0f;
}

bool ResistanceCalibrator::enableGateDriver() {
    /* The timer master-output-enable (MOE) may have been cleared by a previous
     * TIM1 break event.  Re-arm the timer outputs before releasing the gate
     * driver, otherwise the drivers are enabled but nothing switches. */
    PWM_ClearBreakFlag();
    PWM_ClearFault();

    GateDriver_EnableOutputs();

    /* Ensure all TIM1 phase channels and the ADC-trigger channel (CH4) are
     * enabled.  During calibration we reconfigure pin modes instead of clearing
     * CCER, so the ADC trigger (TIM1_TRGO = OC4REF) keeps running. */
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
        if (ready && !fault) {
            uint32_t bdtr = TIM1->BDTR;
            char okmsg[80];
            std::snprintf(okmsg, sizeof(okmsg),
                          "[RES CAL] gate driver ready | ready=Y fault=N MOE=%lu BIF=%lu BKF=%lu",
                          (bdtr >> 15) & 1UL,
                          (TIM1->SR >> 7) & 1UL,
                          (TIM1->SR >> 6) & 1UL);
            Telemetry::log("print", okmsg);
            return true;
        }
        HAL_Delay(5);
    }

    uint32_t bdtr = TIM1->BDTR;
    uint32_t sr   = TIM1->SR;
    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] FAIL: gate driver not ready or fault latched | ready=%s fault=%s MOE=%lu BIF=%lu BKF=%lu",
                  ready ? "Y" : "N",
                  fault ? "Y" : "N",
                  (bdtr >> 15) & 1UL,
                  (sr >> 7) & 1UL,
                  (sr >> 6) & 1UL);
    Telemetry::log("print", msg);
    GateDriver_DisableOutputs();
    return false;
}

void ResistanceCalibrator::configureHardwareImpl(const bool is_high[3],
                                                 const bool is_low[3],
                                                 float duty_pct) {
    /* duty of the high phase: duty_pct % of Vdc appears line-to-line. */
    const uint32_t pulse = static_cast<uint32_t>((duty_pct * static_cast<float>(CAL_ARR)) / 100.0f);

    /* Pin/function mapping note:
     * The main.h pin names are swapped relative to the TIM1 channel assignment:
     *   PH_x_LOW_Pin  is connected to TIM1_CHx  and drives the HIGH-SIDE MOSFET.
     *   PH_x_HIGH_Pin is connected to TIM1_CHxN and drives the LOW-SIDE MOSFET.
     * Therefore:
     *   high-side ON  -> PH_x_LOW_Pin high  / TIM1_CHx active
     *   low-side  ON  -> PH_x_HIGH_Pin high / TIM1_CHxN active
     *
     * We deliberately do NOT touch CCER here.  The ADC injected group is
     * triggered by TIM1_TRGO = OC4REF; gating CCER even briefly can stop the
     * current-sense ISR.  Instead, all phase channels are kept enabled and we
     * control the bridge state by changing GPIO modes and compare values. */

    /* Update compare registers first.  High phase gets the duty pulse; all
     * other phases are set to 0 so their timer outputs are inactive. */
    TIM1->CCR1 = is_high[0] ? pulse : 0;
    TIM1->CCR2 = is_high[1] ? pulse : 0;
    TIM1->CCR3 = is_high[2] ? pulse : 0;

    /* Step 1: turn every phase OFF by driving both pins low.  This prevents
     * shoot-through while we change pin modes in the next step.  The brief
     * high-Z interval is safe because the motor current freewheels through
     * diodes. */
    setPin(PH_U_LOW_Pin,  false, false);
    setPin(PH_U_HIGH_Pin, false, false);
    setPin(PH_V_LOW_Pin,  false, false);
    setPin(PH_V_HIGH_Pin, false, false);
    setPin(PH_W_LOW_Pin,  false, false);
    setPin(PH_W_HIGH_Pin, false, false);

    /* Step 2: apply the new pin configuration.
     *  - high phase: both pins in AF for complementary PWM with dead time
     *  - low phase:  high-side pin GPIO low, low-side pin GPIO high (DC on)
     *  - high-Z:     both pins GPIO low (already done above) */

    /* Phase U */
    if (is_high[0]) {
        setPin(PH_U_LOW_Pin,  true, false);  /* high-side PWM (TIM1_CH1) */
        setPin(PH_U_HIGH_Pin, true, false);  /* low-side complementary (TIM1_CH1N) */
    } else if (is_low[0]) {
        setPin(PH_U_LOW_Pin,  false, false); /* high-side off */
        setPin(PH_U_HIGH_Pin, false, true);  /* low-side on */
    }

    /* Phase V */
    if (is_high[1]) {
        setPin(PH_V_LOW_Pin,  true, false);
        setPin(PH_V_HIGH_Pin, true, false);
    } else if (is_low[1]) {
        setPin(PH_V_LOW_Pin,  false, false);
        setPin(PH_V_HIGH_Pin, false, true);
    }

    /* Phase W */
    if (is_high[2]) {
        setPin(PH_W_LOW_Pin,  true, false);
        setPin(PH_W_HIGH_Pin, true, false);
    } else if (is_low[2]) {
        setPin(PH_W_LOW_Pin,  false, false);
        setPin(PH_W_HIGH_Pin, false, true);
    }

    /* A DESAT trip can latch between enableGateDriver() and the first PWM edge.
     * Check immediately so we don't run the PI on stale/frozen current. */
    if (GateDriver_IsFault()) {
        uint32_t bdtr = TIM1->BDTR;
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "[RES CAL] FAIL: gate-driver fault after enabling PWM | MOE=%lu BIF=%lu BKF=%lu",
                      (bdtr >> 15) & 1UL,
                      (TIM1->SR >> 7) & 1UL,
                      (TIM1->SR >> 6) & 1UL);
        Telemetry::log("print", msg);
        fail("[RES CAL] FAIL: DESAT/gate-driver fault latched after PWM enable");
        return;
    }
    if ((TIM1->BDTR & TIM_BDTR_MOE) == 0U) {
        Telemetry::log("print", "[RES CAL] FAIL: TIM1 MOE cleared after enabling PWM (break event)");
        fail("[RES CAL] FAIL: TIM1 break event cleared MOE");
        return;
    }
}

void ResistanceCalibrator::configureHardware(float bus_pct) {
    const Pair pair = m_pairs[m_pair_index];
    bool is_high[3] = {false, false, false};
    bool is_low[3]  = {false, false, false};

    switch (pair) {
        case Pair::UV:
            is_high[0] = true; is_low[1] = true;
            break;
        case Pair::UW:
            is_high[0] = true; is_low[2] = true;
            break;
        case Pair::VW:
            is_high[1] = true; is_low[2] = true;
            break;
    }

    configureHardwareImpl(is_high, is_low, bus_pct);
}

void ResistanceCalibrator::configureHardware(DirectedPair pair, float duty_pct) {
    bool is_high[3] = {false, false, false};
    bool is_low[3]  = {false, false, false};

    switch (pair) {
        case DirectedPair::UV: is_high[0] = true; is_low[1] = true; break;
        case DirectedPair::VU: is_high[1] = true; is_low[0] = true; break;
        case DirectedPair::UW: is_high[0] = true; is_low[2] = true; break;
        case DirectedPair::WU: is_high[2] = true; is_low[0] = true; break;
        case DirectedPair::VW: is_high[1] = true; is_low[2] = true; break;
        case DirectedPair::WV: is_high[2] = true; is_low[1] = true; break;
    }

    configureHardwareImpl(is_high, is_low, duty_pct);
}

void ResistanceCalibrator::configureHardware(DoublePair pair, float duty_pct) {
    bool is_high[3] = {false, false, false};
    bool is_low[3]  = {false, false, false};

    switch (pair) {
        case DoublePair::U_VW: is_high[0] = true; is_low[1] = true; is_low[2] = true; break;
        case DoublePair::V_UW: is_low[0] = true; is_high[1] = true; is_low[2] = true; break;
        case DoublePair::W_UV: is_low[0] = true; is_low[1] = true; is_high[2] = true; break;
    }

    configureHardwareImpl(is_high, is_low, duty_pct);
}

void ResistanceCalibrator::restoreHardware() {
    /* 1. Disable all timer outputs -> stop switching immediately. */
    TIM1->CCER &= ~(TIM_CCER_CC1E | TIM_CCER_CC1NE |
                    TIM_CCER_CC2E | TIM_CCER_CC2NE |
                    TIM_CCER_CC3E | TIM_CCER_CC3NE);

    /* 2. Force all gate-driver inputs low so all MOSFETs are off. */
    GPIOE->BSRR = (PH_U_HIGH_Pin | PH_V_HIGH_Pin | PH_W_HIGH_Pin |
                   PH_U_LOW_Pin  | PH_V_LOW_Pin  | PH_W_LOW_Pin) << 16U;

    uint32_t moder = GPIOE->MODER;
    const uint32_t all_pins_mask =
        (3U << (pinNumber(PH_U_HIGH_Pin) * 2U)) | (3U << (pinNumber(PH_U_LOW_Pin) * 2U)) |
        (3U << (pinNumber(PH_V_HIGH_Pin) * 2U)) | (3U << (pinNumber(PH_V_LOW_Pin) * 2U)) |
        (3U << (pinNumber(PH_W_HIGH_Pin) * 2U)) | (3U << (pinNumber(PH_W_LOW_Pin) * 2U));
    moder &= ~all_pins_mask;
    moder |= (1U << (pinNumber(PH_U_HIGH_Pin) * 2U)) | (1U << (pinNumber(PH_U_LOW_Pin) * 2U)) |
             (1U << (pinNumber(PH_V_HIGH_Pin) * 2U)) | (1U << (pinNumber(PH_V_LOW_Pin) * 2U)) |
             (1U << (pinNumber(PH_W_HIGH_Pin) * 2U)) | (1U << (pinNumber(PH_W_LOW_Pin) * 2U));
    GPIOE->MODER = moder;

    /* 3. Disable gate driver outputs. */
    GateDriver_DisableOutputs();

    /* 4. Restore timer registers including CCER (which keeps TIM1_CH4 and any
     * other previous output enables intact so the ADC TRGO source is preserved). */
    TIM1->CCR1 = m_saved_ccr1;
    TIM1->CCR2 = m_saved_ccr2;
    TIM1->CCR3 = m_saved_ccr3;
    TIM1->PSC  = m_saved_psc;
    TIM1->ARR  = m_saved_arr;
    TIM1->BDTR = (m_saved_bdtr & ~TIM_BDTR_DTG) | (TIM1->BDTR & TIM_BDTR_DTG);
    TIM1->BDTR = m_saved_bdtr;
    TIM1->CCER = m_saved_ccer;

    /* 5. Restore GPIO to original alternate-function modes. */
    GPIOE->MODER = m_saved_gpioe_moder;

    /* 6. Leave the SPWM update interrupt disabled until open-loop starts again. */
    TIM1->DIER &= ~TIM_DIER_UIE;
    HAL_NVIC_DisableIRQ(TIM1_UP_IRQn);
}

void ResistanceCalibrator::finishPairMeasurement() {
    const Pair pair = m_pairs[m_pair_index];

    float v[NUM_POINTS];
    float i[NUM_POINTS];
    float vdc_avg = 0.0f;
    float i_active_max = 0.0f;

    for (uint8_t pt = 0; pt < NUM_POINTS; ++pt) {
        if (m_sample_count[pt] < MIN_SAMPLES) {
            fail("[RES CAL] FAIL: not enough samples");
            return;
        }

        const float vdc = m_sum_vdc[pt] / static_cast<float>(m_sample_count[pt]);
        const float i_active = m_sum_i_active[pt] / static_cast<float>(m_sample_count[pt]);
        const float i_inactive = std::fabs(
            m_sum_i_inactive[pt] / static_cast<float>(m_sample_count[pt]));

        /* The inactive (high-Z) phase should carry essentially zero current. */
        const float max_inactive = std::max(
            MAX_INACTIVE_CURRENT_MIN_A, std::fabs(i_active) * MAX_INACTIVE_CURRENT_RATIO);
        if (i_inactive > max_inactive) {
            char abuf[16], ibuf[16];
            fmtFloat3(abuf, sizeof(abuf), std::fabs(i_active));
            fmtFloat3(ibuf, sizeof(ibuf), i_inactive);
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                          "[RES CAL] FAIL: %s inactive current %s A exceeds limit (active %s A)",
                          pairName(pair), ibuf, abuf);
            fail(msg);
            return;
        }

        const float duty = m_sum_duty[pt] / static_cast<float>(m_sample_count[pt]);
        v[pt] = (duty / 100.0f) * vdc;
        i[pt] = i_active;
        vdc_avg += vdc;
        i_active_max = std::max(i_active_max, std::fabs(i_active));
    }
    vdc_avg /= static_cast<float>(NUM_POINTS);

    /* Print the raw (V, I) points used for the fit. */
    {
        char msg[192];
        int len = std::snprintf(msg, sizeof(msg), "[RES CAL] %s fit data: ", pairName(pair));
        for (uint8_t pt = 0; pt < NUM_POINTS; ++pt) {
            char vbuf[16], ibuf[16];
            fmtFloat3(vbuf, sizeof(vbuf), v[pt]);
            fmtFloat3(ibuf, sizeof(ibuf), i[pt]);
            len += std::snprintf(msg + len, sizeof(msg) - len,
                                 "(V=%sV I=%sA)%s", vbuf, ibuf,
                                 (pt + 1U < NUM_POINTS) ? ", " : "");
        }
        Telemetry::log("print", msg);
    }

    /* Linear regression: V = R_ll * I + V_offset.
     * We want the slope R_ll; V_offset is discarded. */
    float sum_v = 0.0f;
    float sum_i = 0.0f;
    float sum_vi = 0.0f;
    float sum_ii = 0.0f;
    for (uint8_t pt = 0; pt < NUM_POINTS; ++pt) {
        sum_v  += v[pt];
        sum_i  += i[pt];
        sum_vi += v[pt] * i[pt];
        sum_ii += i[pt] * i[pt];
    }

    const float n = static_cast<float>(NUM_POINTS);
    const float denom = n * sum_ii - sum_i * sum_i;

    {
        char s1[16], s2[16], s3[16], s4[16], dbuf[16];
        fmtFloat3(s1, sizeof(s1), sum_v);
        fmtFloat3(s2, sizeof(s2), sum_i);
        fmtFloat3(s3, sizeof(s3), sum_vi);
        fmtFloat3(s4, sizeof(s4), sum_ii);
        fmtFloat3(dbuf, sizeof(dbuf), denom);
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      "[RES CAL] %s fit math: sumV=%s sumI=%s sumVI=%s sumII=%s denom=%s",
                      pairName(pair), s1, s2, s3, s4, dbuf);
        Telemetry::log("print", msg);
    }

    if (std::fabs(denom) < 1e-9f || !std::isfinite(denom)) {
        fail("[RES CAL] FAIL: current did not vary enough between points");
        return;
    }

    const float r_ll = (n * sum_vi - sum_v * sum_i) / denom;
    {
        char rbuf[16];
        fmtFloat4(rbuf, sizeof(rbuf), r_ll * 1000.0f);
        char msg[96];
        std::snprintf(msg, sizeof(msg), "[RES CAL] %s fit result: R_ll=%s mohm", pairName(pair), rbuf);
        Telemetry::log("print", msg);
    }
    if (r_ll <= 0.0f || !std::isfinite(r_ll)) {
        fail("[RES CAL] FAIL: computed resistance is non-positive; increase current/voltage");
        return;
    }

    const float v_offset = (sum_v - r_ll * sum_i) / n;

    const float r_phase = r_ll * 0.5f;
    const int idx = pairIndex(pair);
    m_results[idx] = r_phase;
    m_result_valid[idx] = true;

    char rll_buf[16], rph_buf[16];
    fmtFloat4(rll_buf, sizeof(rll_buf), r_ll * 1000.0f);
    fmtFloat4(rph_buf, sizeof(rph_buf), r_phase * 1000.0f);
    char ibuf[16], vbuf[16], offbuf[16];
    fmtFloat3(ibuf, sizeof(ibuf), i_active_max);
    fmtFloat3(vbuf, sizeof(vbuf), vdc_avg);
    fmtFloat3(offbuf, sizeof(offbuf), v_offset);
    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] %s: R_ll=%s mohm  R_phase=%s mohm  Imax=%s A  Vdc=%s V  V_off=%s V",
                  pairName(pair), rll_buf, rph_buf, ibuf, vbuf, offbuf);
    Telemetry::log("print", msg);

    enterState(State::NEXT_PAIR);
}

void ResistanceCalibrator::finishTopologyDiff() {
    /* Count valid points and build the high-current fit set. */
    uint8_t valid_count = 0;
    for (uint8_t i = 0; i < TOPOLOGY_DIFF_POINTS; ++i) {
        if (m_td_valid[i]) ++valid_count;
    }
    if (valid_count < 4U) {
        fail("[RES TD] FAIL: not enough valid current points");
        return;
    }

    /* Print raw data. */
    {
        char msg[256];
        int len = std::snprintf(msg, sizeof(msg), "[RES TD] raw data: ");
        for (uint8_t i = 0; i < TOPOLOGY_DIFF_POINTS; ++i) {
            if (!m_td_valid[i]) continue;
            char ibuf[16], v1buf[16], v2buf[16];
            fmtFloat3(ibuf, sizeof(ibuf), m_td_i[i]);
            fmtFloat3(v1buf, sizeof(v1buf), m_td_v1[i]);
            fmtFloat3(v2buf, sizeof(v2buf), m_td_v2[i]);
            len += std::snprintf(msg + len, sizeof(msg) - len,
                                 "(I=%s V1=%s V2=%s)%s",
                                 ibuf, v1buf, v2buf,
                                 (i + 1U < TOPOLOGY_DIFF_POINTS) ? ", " : "");
        }
        Telemetry::log("print", msg);
    }

    /* Fit ΔV = V1 - V2 vs I over the upper half of the current range. */
    const float i_threshold = 0.5f * m_td_targets[TOPOLOGY_DIFF_POINTS - 1U];
    float sum_i = 0.0f;
    float sum_dv = 0.0f;
    float sum_idv = 0.0f;
    float sum_ii = 0.0f;
    uint32_t n = 0;
    for (uint8_t i = 0; i < TOPOLOGY_DIFF_POINTS; ++i) {
        if (!m_td_valid[i] || m_td_i[i] < i_threshold) continue;
        const float dv = m_td_v1[i] - m_td_v2[i];
        sum_i   += m_td_i[i];
        sum_dv  += dv;
        sum_idv += m_td_i[i] * dv;
        sum_ii  += m_td_i[i] * m_td_i[i];
        ++n;
    }

    if (n < 3U) {
        fail("[RES TD] FAIL: not enough high-current points for fit");
        return;
    }

    const float nf = static_cast<float>(n);
    const float denom = nf * sum_ii - sum_i * sum_i;
    if (std::fabs(denom) < 1e-9f || !std::isfinite(denom)) {
        fail("[RES TD] FAIL: current did not vary enough for fit");
        return;
    }

    const float slope = (nf * sum_idv - sum_i * sum_dv) / denom;
    m_r_total = 2.0f * slope;

    {
        char sbuf[16], rbuf[16];
        fmtFloat4(sbuf, sizeof(sbuf), slope * 1000.0f);
        fmtFloat4(rbuf, sizeof(rbuf), m_r_total * 1000.0f);
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "[RES TD] fit: slope=%s mV/A  R_total=%s mohm (n=%lu)",
                      sbuf, rbuf, static_cast<unsigned long>(n));
        Telemetry::log("print", msg);
    }

    if (m_r_total <= 0.0f || !std::isfinite(m_r_total)) {
        fail("[RES TD] FAIL: R_total is non-positive");
        return;
    }

    /* Build the inverter-drop LUT. */
    m_vce_lut_count = 0;
    for (uint8_t i = 0; i < TOPOLOGY_DIFF_POINTS; ++i) {
        if (!m_td_valid[i]) continue;
        m_vce_lut_i[m_vce_lut_count] = m_td_i[i];
        m_vce_lut_v[m_vce_lut_count] = m_td_v1[i] - 2.0f * m_td_i[i] * m_r_total;
        ++m_vce_lut_count;
    }

    /* Print the LUT. */
    {
        char msg[256];
        int len = std::snprintf(msg, sizeof(msg), "[RES TD] Vce LUT: ");
        for (uint8_t i = 0; i < m_vce_lut_count; ++i) {
            char ibuf[16], vbuf[16];
            fmtFloat3(ibuf, sizeof(ibuf), m_vce_lut_i[i]);
            fmtFloat3(vbuf, sizeof(vbuf), m_vce_lut_v[i]);
            len += std::snprintf(msg + len, sizeof(msg) - len,
                                 "(I=%s Vce=%s)%s", ibuf, vbuf,
                                 (i + 1U < m_vce_lut_count) ? ", " : "");
        }
        Telemetry::log("print", msg);
    }

    restoreHardware();
    reportTopologyDiffResults();
    enterState(State::DONE);
}


void ResistanceCalibrator::reportResults() {
    float sum_ll = 0.0f;
    float sum_ph = 0.0f;
    uint32_t count = 0;
    for (int i = 0; i < 3; ++i) {
        if (m_result_valid[i]) {
            sum_ll += m_results[i] * 2.0f; /* m_results stores phase resistance */
            sum_ph += m_results[i];
            ++count;
        }
    }
    m_average_r_phase = (count > 0) ? (sum_ph / static_cast<float>(count)) : 0.0f;
    const float avg_r_ll = (count > 0) ? (sum_ll / static_cast<float>(count)) : 0.0f;

    char uv[16], uw[16], vw[16], avg[16];
    fmtFloat4(uv, sizeof(uv), m_results[0] * 2000.0f); /* line-line */
    fmtFloat4(uw, sizeof(uw), m_results[1] * 2000.0f);
    fmtFloat4(vw, sizeof(vw), m_results[2] * 2000.0f);
    fmtFloat4(avg, sizeof(avg), avg_r_ll * 1000.0f);

    char msg[160];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] DONE: Rll_uv=%s Rll_uw=%s Rll_vw=%s Rll_avg=%s mohm",
                  m_result_valid[0] ? uv : "--",
                  m_result_valid[1] ? uw : "--",
                  m_result_valid[2] ? vw : "--",
                  avg);
    Telemetry::log("print", msg);

    Telemetry::log("r_ll_uv", m_results[0] * 2.0f);
    Telemetry::log("r_ll_uw", m_results[1] * 2.0f);
    Telemetry::log("r_ll_vw", m_results[2] * 2.0f);
    Telemetry::log("r_phase_avg", m_average_r_phase);
}

void ResistanceCalibrator::reportTopologyDiffResults() {
    char rbuf[16];
    fmtFloat4(rbuf, sizeof(rbuf), m_r_total * 1000.0f);
    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "[RES TD] DONE: R_total=%s mohm  LUT points=%u",
                  rbuf, static_cast<unsigned>(m_vce_lut_count));
    Telemetry::log("print", msg);
    Telemetry::log("r_total", m_r_total);
}

void ResistanceCalibrator::advanceTopologyDiffSubstep() {
    ++m_substep_index;
    if (m_substep_index < 6U) {
        /* Next single-pair direction at the same current setpoint.
         * Keep the PI state so the loop does not restart from zero each time. */
        resetSubstepAccumulators();
        configureHardware(static_cast<DirectedPair>(m_substep_index), m_pi_duty);
        enterState(State::SETTLE);
    } else if (m_substep_index < 9U) {
        /* Next double-pair topology at the same current setpoint. */
        resetSubstepAccumulators();
        configureHardware(static_cast<DoublePair>(m_substep_index - 6U), m_pi_duty);
        enterState(State::SETTLE);
    } else {
        /* Finished all substeps for this current point. */
        const uint8_t pt = m_topology_point_index;
        if (m_td_v1_count > 0U && m_td_v2_count > 0U) {
            m_td_i[pt] = m_td_targets[pt];
            m_td_v1[pt] = m_td_v1_sum / static_cast<float>(m_td_v1_count);
            m_td_v2[pt] = m_td_v2_sum / static_cast<float>(m_td_v2_count);
            m_td_valid[pt] = true;
        }

        m_td_v1_sum = 0.0f;
        m_td_v2_sum = 0.0f;
        m_td_v1_count = 0;
        m_td_v2_count = 0;
        ++m_topology_point_index;
        m_substep_index = 0;

        if (m_topology_point_index < TOPOLOGY_DIFF_POINTS) {
            /* Move to the next current setpoint.  Keep PI integral/duty so the
             * transition is smooth; the controller will adapt to the new setpoint. */
            resetSubstepAccumulators();
            configureHardware(DirectedPair::UV, m_pi_duty);
            enterState(State::SETTLE);
        } else {
            enterState(State::FINISH_PAIR);
        }
    }
}

void ResistanceCalibrator::update() {
    if (m_state == State::IDLE || m_state == State::DONE ||
        m_state == State::FAIL) {
        return;
    }

    ++m_update_calls;
    const uint32_t now_ms = HAL_GetTick();

    /* Abort on any active Critical or High fault. */
    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::log("print", "[RES CAL] FAIL: fault detected");
        restoreHardware();
        enterState(State::FAIL);
        return;
    }

    if (m_state == State::ENABLE) {
        /* Save current timer and GPIO state. */
        m_saved_arr = TIM1->ARR;
        m_saved_psc = TIM1->PSC;
        m_saved_ccer = TIM1->CCER;
        m_saved_ccr1 = TIM1->CCR1;
        m_saved_ccr2 = TIM1->CCR2;
        m_saved_ccr3 = TIM1->CCR3;
        m_saved_bdtr = TIM1->BDTR;
        m_saved_gpioe_moder = GPIOE->MODER;

        /* Disable the SPWM update interrupt; we will drive the timer directly. */
        TIM1->DIER &= ~TIM_DIER_UIE;
        HAL_NVIC_DisableIRQ(TIM1_UP_IRQn);

        /* Set calibration frequency (~8 kHz).
         * TIM1CLK = 275 MHz, center-aligned => f_sw = 275 MHz / (2 * ARR).
         * ARR = 17186 => ~8.0 kHz. */
        TIM1->PSC = 0U;
        TIM1->ARR = CAL_ARR;

        /* Static 1 us dead time during calibration.
         * DTG = 0xC3 -> 110 encoding, (32 + 3) * 8 * t_DTS = ~1018 ns. */
        TIM1->BDTR = (TIM1->BDTR & ~TIM_BDTR_DTG) | 0xC3U;

        if (!enableGateDriver()) {
            restoreHardware();
            enterState(State::FAIL);
            return;
        }

        if (m_mode == Mode::TOPOLOGY_DIFF) {
            m_topology_point_index = 0;
            m_substep_index = 0;
            m_td_v1_sum = 0.0f;
            m_td_v2_sum = 0.0f;
            m_td_v1_count = 0;
            m_td_v2_count = 0;
            resetSubstepAccumulators();
            m_pi_integral = 0.0f;
            m_pi_duty = PI_MIN_DUTY;
            m_pi_last_ms = now_ms;
            configureHardware(DirectedPair::UV, m_pi_duty);
        } else {
            m_point_index = 0;
            if (m_mode == Mode::VOLTAGE_STEP) {
                configureHardware(m_targets[m_point_index]);
            } else {
                m_pi_integral = 0.0f;
                m_pi_duty = PI_MIN_DUTY;
                m_pi_last_ms = now_ms;
                configureHardware(m_pi_duty);
            }
        }

        /* configureHardware() can transition to FAIL if DESAT/FLT trips
         * immediately.  Do not enter SETTLE if it failed. */
        if (m_state == State::FAIL) {
            return;
        }

        enterState(State::SETTLE);
        return;
    }

    const uint32_t elapsed_ms = now_ms - m_state_enter_ms;
    if (elapsed_ms > m_timeout_ms) {
        Telemetry::log("print", "[RES CAL] FAIL: timeout");
        restoreHardware();
        enterState(State::FAIL);
        return;
    }

    const Pair pair = m_pairs[m_pair_index];

    if (m_state == State::SETTLE) {
        if (elapsed_ms >= SETTLE_TIME_MS) {
            char msg[128];
            if (m_mode == Mode::TOPOLOGY_DIFF) {
                const uint8_t pt = m_topology_point_index;
                resetSubstepAccumulators();
                m_pi_last_ms = now_ms;
                char ibuf[16];
                fmtFloat3(ibuf, sizeof(ibuf), m_td_targets[pt]);
                if (m_substep_index < 6) {
                    std::snprintf(msg, sizeof(msg),
                                  "[RES TD] I=%s A  substep %u/9: %s",
                                  ibuf, static_cast<unsigned>(m_substep_index + 1U),
                                  directedPairName(static_cast<DirectedPair>(m_substep_index)));
                } else {
                    std::snprintf(msg, sizeof(msg),
                                  "[RES TD] I=%s A  substep %u/9: %s",
                                  ibuf, static_cast<unsigned>(m_substep_index + 1U),
                                  doublePairName(static_cast<DoublePair>(m_substep_index - 6U)));
                }
            } else {
                const uint8_t pt = m_point_index;
                resetMeasurementAccumulators(pt);
                if (m_mode == Mode::VOLTAGE_STEP) {
                    char vbuf[16];
                    fmtFloat3(vbuf, sizeof(vbuf),
                              m_targets[pt] * 0.01f * dcLinkVoltageSensor().voltage());
                    std::snprintf(msg, sizeof(msg),
                                  "[RES CAL] %s point %u/%u: target Vll=%s V",
                                  pairName(pair), static_cast<unsigned>(pt + 1U),
                                  static_cast<unsigned>(NUM_POINTS), vbuf);
                } else {
                    char ibuf[16];
                    fmtFloat3(ibuf, sizeof(ibuf), m_targets[pt]);
                    std::snprintf(msg, sizeof(msg),
                                  "[RES CAL] %s point %u/%u: target I=%s A",
                                  pairName(pair), static_cast<unsigned>(pt + 1U),
                                  static_cast<unsigned>(NUM_POINTS), ibuf);
                }
            }
            Telemetry::log("print", msg);
            m_update_calls = 0;
            m_sample_calls = 0;
            m_last_rate_log_ms = now_ms;
            m_last_sample_ms = now_ms;
            enterState(State::MEASURE);
        }
        return;
    }

    if (m_state == State::MEASURE) {
        /* If the ADC trigger has stopped, the PI will wind up on stale data and
         * we risk a hardware overcurrent.  Abort if no new sample arrives. */
        if (now_ms - m_last_sample_ms > 100U) {
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                          "[RES CAL] FAIL: no new ADC sample for %lu ms (stale current)",
                          static_cast<unsigned long>(now_ms - m_last_sample_ms));
            Telemetry::log("print", msg);
            restoreHardware();
            enterState(State::FAIL);
            return;
        }

        if (m_mode == Mode::TOPOLOGY_DIFF) {
            const uint8_t pt = m_topology_point_index;
            const float i_set = m_td_targets[pt];
            float iu = 0.0f, iv = 0.0f, iw = 0.0f;

            if (phaseCurrentADC().sample(iu, iv, iw)) {
                ++m_sample_calls;
                m_last_sample_ms = now_ms;

                float i_active = 0.0f;
                if (m_substep_index < 6U) {
                    const DirectedPair dp = static_cast<DirectedPair>(m_substep_index);
                    i_active = activeCurrentForDirectedPair(iu, iv, iw, dp);
                    m_sub_i_active_sum += i_active;
                    m_sub_i_ret1_sum += inactiveCurrentForDirectedPair(iu, iv, iw, dp);
                } else {
                    const DoublePair dp = static_cast<DoublePair>(m_substep_index - 6U);
                    switch (dp) {
                        case DoublePair::U_VW:
                            i_active = -iu;
                            m_sub_i_active_sum += i_active;
                            m_sub_i_ret1_sum += iv;
                            m_sub_i_ret2_sum += iw;
                            break;
                        case DoublePair::V_UW:
                            i_active = -iv;
                            m_sub_i_active_sum += i_active;
                            m_sub_i_ret1_sum += iu;
                            m_sub_i_ret2_sum += iw;
                            break;
                        case DoublePair::W_UV:
                            i_active = -iw;
                            m_sub_i_active_sum += i_active;
                            m_sub_i_ret1_sum += iu;
                            m_sub_i_ret2_sum += iv;
                            break;
                    }
                }

                const float vdc = dcLinkVoltageSensor().voltage();
                m_sub_vdc_sum += vdc;
                ++m_sub_sample_count;

                /* PI current controller: update duty every sample. */
                const float dt_s = (m_pi_last_ms == 0) ? 0.001f :
                    static_cast<float>(now_ms - m_pi_last_ms) * 0.001f;
                m_pi_last_ms = now_ms;

                const float error = i_set - i_active;
                m_pi_integral += PI_KI * error * dt_s;
                float duty = PI_KP * error + m_pi_integral;
                if (duty > MAX_BUS_PCT) {
                    duty = MAX_BUS_PCT;
                    if (m_pi_integral > 0.0f) m_pi_integral -= PI_KI * error * dt_s;
                } else if (duty < PI_MIN_DUTY) {
                    duty = PI_MIN_DUTY;
                    if (m_pi_integral < 0.0f) m_pi_integral -= PI_KI * error * dt_s;
                }
                m_pi_duty = duty;

                if (m_substep_index < 6U) {
                    configureHardware(static_cast<DirectedPair>(m_substep_index), duty);
                } else {
                    configureHardware(static_cast<DoublePair>(m_substep_index - 6U), duty);
                }
                m_sub_duty_sum += duty;

                if (m_max_current_a > 0.0f && std::fabs(i_active) > m_max_current_a) {
                    char abuf[16], mbuf[16];
                    fmtFloat3(abuf, sizeof(abuf), i_active);
                    fmtFloat3(mbuf, sizeof(mbuf), m_max_current_a);
                    char msg[96];
                    std::snprintf(msg, sizeof(msg),
                                  "[RES TD] FAIL: overcurrent %s A > limit %s A",
                                  abuf, mbuf);
                    Telemetry::log("print", msg);
                    restoreHardware();
                    enterState(State::FAIL);
                    return;
                }
            }

            /* Diagnostic: log actual call and sample rates every 250 ms. */
            if (now_ms - m_last_rate_log_ms >= 250U) {
                const uint32_t dt_ms = now_ms - m_last_rate_log_ms;
                const float update_hz = static_cast<float>(m_update_calls) * 1000.0f /
                                        static_cast<float>(dt_ms);
                const float sample_hz = static_cast<float>(m_sample_calls) * 1000.0f /
                                        static_cast<float>(dt_ms);
                char ubuf[16], sbuf[16];
                fmtFloat3(ubuf, sizeof(ubuf), update_hz);
                fmtFloat3(sbuf, sizeof(sbuf), sample_hz);
                const char* name = (m_substep_index < 6U)
                    ? directedPairName(static_cast<DirectedPair>(m_substep_index))
                    : doublePairName(static_cast<DoublePair>(m_substep_index - 6U));
                char msg[128];
                std::snprintf(msg, sizeof(msg),
                              "[RES TD] %s timing: update=%s Hz  sample=%s Hz  n_samp=%lu",
                              name, ubuf, sbuf,
                              static_cast<unsigned long>(m_sample_calls));
                Telemetry::log("print", msg);
                m_update_calls = 0;
                m_sample_calls = 0;
                m_last_rate_log_ms = now_ms;
            }

            if (elapsed_ms >= MEASURE_TIME_MS && m_sub_sample_count >= MIN_SAMPLES) {
                const float vdc_avg = m_sub_vdc_sum / static_cast<float>(m_sub_sample_count);
                const float duty_avg = m_sub_duty_sum / static_cast<float>(m_sub_sample_count);
                const float v_sub = (duty_avg / 100.0f) * vdc_avg;
                const float i_active_avg = m_sub_i_active_sum /
                                           static_cast<float>(m_sub_sample_count);

                if (m_substep_index < 6U) {
                    const DirectedPair dp = static_cast<DirectedPair>(m_substep_index);
                    const float i_inactive_avg = std::fabs(
                        m_sub_i_ret1_sum / static_cast<float>(m_sub_sample_count));
                    const float max_inactive = std::max(
                        MAX_INACTIVE_CURRENT_MIN_A,
                        std::fabs(i_active_avg) * MAX_INACTIVE_CURRENT_RATIO);
                    if (i_inactive_avg > max_inactive) {
                        char abuf[16], ibuf[16];
                        fmtFloat3(abuf, sizeof(abuf), std::fabs(i_active_avg));
                        fmtFloat3(ibuf, sizeof(ibuf), i_inactive_avg);
                        char msg[96];
                        std::snprintf(msg, sizeof(msg),
                                      "[RES TD] FAIL: %s inactive current %s A exceeds limit (active %s A)",
                                      directedPairName(dp), ibuf, abuf);
                        fail(msg);
                        return;
                    }

                    m_td_v1_sum += v_sub;
                    ++m_td_v1_count;

                    char vbuf[16], ibuf[16], iabuf[16];
                    fmtFloat3(vbuf, sizeof(vbuf), v_sub);
                    fmtFloat3(ibuf, sizeof(ibuf), i_active_avg);
                    fmtFloat3(iabuf, sizeof(iabuf), i_inactive_avg);
                    char msg[128];
                    std::snprintf(msg, sizeof(msg),
                                  "[RES TD] %s done: V=%s V  Iact=%s A  Iinact=%s A",
                                  directedPairName(dp), vbuf, ibuf, iabuf);
                    Telemetry::log("print", msg);
                } else {
                    const DoublePair dp = static_cast<DoublePair>(m_substep_index - 6U);
                    const float ret1_avg = m_sub_i_ret1_sum /
                                           static_cast<float>(m_sub_sample_count);
                    const float ret2_avg = m_sub_i_ret2_sum /
                                           static_cast<float>(m_sub_sample_count);

                    /* Symmetry check using averaged currents.
                     * Allow +/-5% of setpoint, but never less than 5 A, so
                     * low-current points are not rejected by noise. */
                    const float sym_tol = std::max(5.0f, 0.05f * std::fabs(i_set));
                    bool sym_ok = true;
                    if (std::fabs(ret1_avg) < 0.1f || std::fabs(ret2_avg) < 0.1f) {
                        sym_ok = false;
                    } else if ((ret1_avg > 0.0f) != (ret2_avg > 0.0f)) {
                        sym_ok = false;
                    } else if (std::fabs(ret1_avg - ret2_avg) > sym_tol) {
                        sym_ok = false;
                    } else if (std::fabs(i_active_avg - (ret1_avg + ret2_avg)) > sym_tol) {
                        sym_ok = false;
                    }

                    if (!sym_ok) {
                        char abuf[16], r1buf[16], r2buf[16];
                        fmtFloat3(abuf, sizeof(abuf), i_active_avg);
                        fmtFloat3(r1buf, sizeof(r1buf), ret1_avg);
                        fmtFloat3(r2buf, sizeof(r2buf), ret2_avg);
                        char msg[128];
                        std::snprintf(msg, sizeof(msg),
                                      "[RES TD] FAIL: %s asymmetry Iact=%s A  ret1=%s A  ret2=%s A",
                                      doublePairName(dp), abuf, r1buf, r2buf);
                        fail(msg);
                        return;
                    }

                    m_td_v2_sum += v_sub;
                    ++m_td_v2_count;

                    char vbuf[16], ibuf[16], r1buf[16], r2buf[16];
                    fmtFloat3(vbuf, sizeof(vbuf), v_sub);
                    fmtFloat3(ibuf, sizeof(ibuf), i_active_avg);
                    fmtFloat3(r1buf, sizeof(r1buf), ret1_avg);
                    fmtFloat3(r2buf, sizeof(r2buf), ret2_avg);
                    char msg[160];
                    std::snprintf(msg, sizeof(msg),
                                  "[RES TD] %s done: V=%s V  Iact=%s A  ret1=%s A  ret2=%s A",
                                  doublePairName(dp), vbuf, ibuf, r1buf, r2buf);
                    Telemetry::log("print", msg);
                }

                advanceTopologyDiffSubstep();
            }
            return;
        }

        /* Original modes (VOLTAGE_STEP and CURRENT_CTRL). */
        const uint8_t pt = m_point_index;

        float iu, iv, iw;
        if (phaseCurrentADC().sample(iu, iv, iw)) {
            ++m_sample_calls;
            m_last_sample_ms = now_ms;
            const float i_active = pairCurrentActive(iu, iv, iw, pair);
            m_sum_i_active[pt] += i_active;
            m_sum_i_inactive[pt] += pairCurrentInactive(iu, iv, iw, pair);
            const float vdc = dcLinkVoltageSensor().voltage();
            m_sum_vdc[pt] += vdc;
            ++m_sample_count[pt];

            if (m_mode == Mode::CURRENT_CTRL) {
                /* PI current controller: update duty every sample to regulate
                 * active current to the target. */
                const float dt_s = (m_pi_last_ms == 0) ? 0.001f :
                    static_cast<float>(now_ms - m_pi_last_ms) * 0.001f;
                m_pi_last_ms = now_ms;

                const float error = m_targets[pt] - i_active;
                m_pi_integral += PI_KI * error * dt_s;
                /* Simple anti-windup: clamp integral when output saturates. */
                float duty = PI_KP * error + m_pi_integral;
                if (duty > MAX_BUS_PCT) {
                    duty = MAX_BUS_PCT;
                    if (m_pi_integral > 0.0f) m_pi_integral -= PI_KI * error * dt_s;
                } else if (duty < PI_MIN_DUTY) {
                    duty = PI_MIN_DUTY;
                    if (m_pi_integral < 0.0f) m_pi_integral -= PI_KI * error * dt_s;
                }
                m_pi_duty = duty;
                configureHardware(m_pi_duty);
                m_sum_duty[pt] += m_pi_duty;
            } else {
                m_sum_duty[pt] += m_targets[pt];
            }

            if (m_max_current_a > 0.0f && std::fabs(i_active) > m_max_current_a) {
                char abuf[16], mbuf[16];
                fmtFloat3(abuf, sizeof(abuf), i_active);
                fmtFloat3(mbuf, sizeof(mbuf), m_max_current_a);
                char msg[96];
                std::snprintf(msg, sizeof(msg),
                              "[RES CAL] FAIL: overcurrent %s A > limit %s A",
                              abuf, mbuf);
                Telemetry::log("print", msg);
                restoreHardware();
                enterState(State::FAIL);
                return;
            }
        }

        /* Diagnostic: log actual call and sample rates every 250 ms. */
        if (now_ms - m_last_rate_log_ms >= 250U) {
            const uint32_t dt_ms = now_ms - m_last_rate_log_ms;
            const float update_hz = static_cast<float>(m_update_calls) * 1000.0f /
                                    static_cast<float>(dt_ms);
            const float sample_hz = static_cast<float>(m_sample_calls) * 1000.0f /
                                    static_cast<float>(dt_ms);
            char ubuf[16], sbuf[16];
            fmtFloat3(ubuf, sizeof(ubuf), update_hz);
            fmtFloat3(sbuf, sizeof(sbuf), sample_hz);
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "[RES CAL] %s timing: update=%s Hz  sample=%s Hz  n_samp=%lu",
                          pairName(pair), ubuf, sbuf,
                          static_cast<unsigned long>(m_sample_calls));
            Telemetry::log("print", msg);
            m_update_calls = 0;
            m_sample_calls = 0;
            m_last_rate_log_ms = now_ms;
        }

        if (elapsed_ms >= MEASURE_TIME_MS && m_sample_count[pt] >= MIN_SAMPLES) {
            const float vdc_pt = m_sum_vdc[pt] / static_cast<float>(m_sample_count[pt]);
            const float iact_pt = m_sum_i_active[pt] / static_cast<float>(m_sample_count[pt]);
            const float iinact_pt = std::fabs(
                m_sum_i_inactive[pt] / static_cast<float>(m_sample_count[pt]));
            const float duty_pt = m_sum_duty[pt] / static_cast<float>(m_sample_count[pt]);
            const float vll_pt = (duty_pt / 100.0f) * vdc_pt;
            char vbuf[16], ibuf[16], iabuf[16], pbuf[16], dbuf[16];
            fmtFloat3(vbuf, sizeof(vbuf), vll_pt);
            fmtFloat3(ibuf, sizeof(ibuf), iact_pt);
            fmtFloat3(iabuf, sizeof(iabuf), iinact_pt);
            fmtFloat3(pbuf, sizeof(pbuf), vdc_pt);
            fmtFloat3(dbuf, sizeof(dbuf), duty_pt);
            char msg[192];
            std::snprintf(msg, sizeof(msg),
                          "[RES CAL] %s point %u/%u done: Vll=%s V  Vdc=%s V  duty=%s %%  Iact=%s A  Iinact=%s A",
                          pairName(pair), static_cast<unsigned>(pt + 1U),
                          static_cast<unsigned>(NUM_POINTS), vbuf, pbuf, dbuf, ibuf, iabuf);
            Telemetry::log("print", msg);

            if (m_point_index + 1U < NUM_POINTS) {
                ++m_point_index;
                if (m_mode == Mode::VOLTAGE_STEP) {
                    configureHardware(m_targets[m_point_index]);
                } else {
                    m_pi_integral = 0.0f;
                    m_pi_duty = PI_MIN_DUTY;
                    m_pi_last_ms = now_ms;
                    configureHardware(m_pi_duty);
                }
                enterState(State::SETTLE);
            } else {
                enterState(State::FINISH_PAIR);
            }
        }
        return;
    }

    if (m_state == State::FINISH_PAIR) {
        if (m_mode == Mode::TOPOLOGY_DIFF) {
            finishTopologyDiff();
        } else {
            finishPairMeasurement();
        }
        return;
    }

    if (m_state == State::NEXT_PAIR) {
        ++m_pair_index;
        if (m_pair_index >= m_num_pairs) {
            restoreHardware();
            reportResults();
            enterState(State::DONE);
            return;
        }
        m_point_index = 0;
        if (m_mode == Mode::VOLTAGE_STEP) {
            configureHardware(m_targets[m_point_index]);
        } else {
            m_pi_integral = 0.0f;
            m_pi_duty = PI_MIN_DUTY;
            m_pi_last_ms = now_ms;
            configureHardware(m_pi_duty);
        }
        enterState(State::SETTLE);
        return;
    }
}

} // namespace Inverter
