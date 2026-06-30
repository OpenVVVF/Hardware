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
#include <cstdarg>
#include <cmath>

namespace Inverter {

static ResistanceCalibrator s_instance;

ResistanceCalibrator& ResistanceCalibrator::instance() {
    return s_instance;
}

ResistanceCalibrator& resistanceCalibrator() {
    return s_instance;
}

namespace {

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

float ResistanceCalibrator::lastResult(Pair pair) const {
    return m_results[pairIndex(pair)];
}

bool ResistanceCalibrator::start(float bus_pct, Pair pair, bool run_all,
                                 uint32_t timeout_ms, float max_current_a) {
    if (isActive()) {
        Telemetry::printf("[RES CAL] already running");
        return false;
    }

    if (openLoopController().isRunning()) {
        Telemetry::printf("[RES CAL] stop the motor before calibration");
        return false;
    }

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::printf("[RES CAL] active faults, cannot start");
        return false;
    }

    if (bus_pct < 0.0f) bus_pct = 0.0f;
    if (bus_pct > MAX_BUS_PCT) {
        Telemetry::printf("[RES CAL] clamped bus_pct to %.2f %%", static_cast<double>(MAX_BUS_PCT));
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

    Telemetry::printf("[RES CAL] starting (%s) max %.4f %% bus, %u points, max I=%.3f A",
                      run_all ? "UV/UW/VW" : pairName(pair),
                      static_cast<double>(bus_pct),
                      static_cast<unsigned>(NUM_POINTS),
                      static_cast<double>(m_max_current_a));
    return true;
}

bool ResistanceCalibrator::startCurrentCtrl(float max_current_a, Pair pair,
                                            bool run_all, uint32_t timeout_ms,
                                            float oc_limit_a) {
    if (isActive()) {
        Telemetry::printf("[RES CAL] already running");
        return false;
    }

    if (openLoopController().isRunning()) {
        Telemetry::printf("[RES CAL] stop the motor before calibration");
        return false;
    }

    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::printf("[RES CAL] active faults, cannot start");
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

    Telemetry::printf("[RES CAL] I-ctrl (%s) target %.3f A, %u points, oc=%.3f A",
                      run_all ? "UV/UW/VW" : pairName(pair),
                      static_cast<double>(max_current_a),
                      static_cast<unsigned>(NUM_POINTS),
                      static_cast<double>(m_max_current_a));
    return true;
}

void ResistanceCalibrator::enterState(State state) {
    m_state = state;
    m_state_enter_ms = HAL_GetTick();
}

void ResistanceCalibrator::stop() {
    if (m_state != State::IDLE && m_state != State::DONE && m_state != State::FAIL) {
        Telemetry::printf("[RES CAL] stopped by user");
        restoreHardware();
        enterState(State::FAIL);
    }
}

void ResistanceCalibrator::fail(const char* reason_fmt, ...) {
    restoreHardware();

    va_list ap;
    va_start(ap, reason_fmt);
    Telemetry::vprintf(reason_fmt, ap);
    va_end(ap);

    enterState(State::FAIL);
}

void ResistanceCalibrator::resetMeasurementAccumulators(uint8_t point) {
    m_sample_count[point] = 0;
    m_sum_i_active[point] = 0.0f;
    m_sum_i_inactive[point] = 0.0f;
    m_sum_vdc[point] = 0.0f;
    m_sum_duty[point] = 0.0f;
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
            Telemetry::printf("[RES CAL] gate driver ready | ready=Y fault=N MOE=%lu BIF=%lu BKF=%lu",
                              (bdtr >> 15) & 1UL,
                              (TIM1->SR >> 7) & 1UL,
                              (TIM1->SR >> 6) & 1UL);
            return true;
        }
        HAL_Delay(5);
    }

    uint32_t bdtr = TIM1->BDTR;
    uint32_t sr   = TIM1->SR;
    Telemetry::printf("[RES CAL] FAIL: gate driver not ready or fault latched | ready=%s fault=%s MOE=%lu BIF=%lu BKF=%lu",
                      ready ? "Y" : "N",
                      fault ? "Y" : "N",
                      (bdtr >> 15) & 1UL,
                      (sr >> 7) & 1UL,
                      (sr >> 6) & 1UL);
    GateDriver_DisableOutputs();
    return false;
}

void ResistanceCalibrator::configureHardware(float bus_pct) {
    const Pair pair = m_pairs[m_pair_index];

    /* duty of the high phase: bus_pct % of Vdc appears line-to-line. */
    const uint32_t pulse = static_cast<uint32_t>((bus_pct * static_cast<float>(CAL_ARR)) / 100.0f);

    uint8_t high_phase = 0;
    uint8_t low_phase = 0;
    uint8_t hz_phase = 0;

    switch (pair) {
        case Pair::UV:
            high_phase = 0; low_phase = 1; hz_phase = 2;
            break;
        case Pair::UW:
            high_phase = 0; low_phase = 2; hz_phase = 1;
            break;
        case Pair::VW:
            high_phase = 1; low_phase = 2; hz_phase = 0;
            break;
    }

    /* Default all compare registers to 0, then set the high-phase pulse.
     * We deliberately do NOT touch CCER.  The ADC injected group is triggered
     * by TIM1_TRGO = OC4REF; gating phase channels via CCER can stop the
     * current-sense ISR.  All phase channels stay enabled; the bridge state
     * is controlled by pin mode and compare value. */
    TIM1->CCR1 = 0;
    TIM1->CCR2 = 0;
    TIM1->CCR3 = 0;
    switch (high_phase) {
        case 0: TIM1->CCR1 = pulse; break;
        case 1: TIM1->CCR2 = pulse; break;
        case 2: TIM1->CCR3 = pulse; break;
    }

    /* Pin/function mapping note:
     * The main.h pin names are swapped relative to the TIM1 channel assignment:
     *   PH_x_LOW_Pin  is connected to TIM1_CHx  and drives the HIGH-SIDE MOSFET.
     *   PH_x_HIGH_Pin is connected to TIM1_CHxN and drives the LOW-SIDE MOSFET.
     * Therefore:
     *   high-side ON  -> PH_x_LOW_Pin high  / TIM1_CHx active
     *   low-side  ON  -> PH_x_HIGH_Pin high / TIM1_CHxN active
     *
     * Configure pins:
     *  - high phase: both pins in AF for complementary PWM with dead time
     *  - low phase:  high-side pin GPIO low, low-side pin GPIO high (DC on)
     *  - high-Z:     both pins GPIO low
     *
     * CCER is left enabled for all channels.  The inactive phases have their
     * pins in GPIO mode, so the timer outputs are ignored. */

    /* Phase U */
    if (hz_phase == 0) {
        setPin(PH_U_LOW_Pin,  false, false); /* high-side off */
        setPin(PH_U_HIGH_Pin, false, false); /* low-side off */
    } else if (low_phase == 0) {
        setPin(PH_U_LOW_Pin,  false, false); /* high-side off */
        setPin(PH_U_HIGH_Pin, false, true);  /* low-side on */
    } else { /* high_phase == 0 */
        setPin(PH_U_LOW_Pin,  true, false);  /* high-side PWM (TIM1_CH1) */
        setPin(PH_U_HIGH_Pin, true, false);  /* low-side complementary (TIM1_CH1N) */
    }

    /* Phase V */
    if (hz_phase == 1) {
        setPin(PH_V_LOW_Pin,  false, false);
        setPin(PH_V_HIGH_Pin, false, false);
    } else if (low_phase == 1) {
        setPin(PH_V_LOW_Pin,  false, false);
        setPin(PH_V_HIGH_Pin, false, true);
    } else { /* high_phase == 1 */
        setPin(PH_V_LOW_Pin,  true, false);
        setPin(PH_V_HIGH_Pin, true, false);
    }

    /* Phase W */
    if (hz_phase == 2) {
        setPin(PH_W_LOW_Pin,  false, false);
        setPin(PH_W_HIGH_Pin, false, false);
    } else if (low_phase == 2) {
        setPin(PH_W_LOW_Pin,  false, false);
        setPin(PH_W_HIGH_Pin, false, true);
    } else { /* high_phase == 2 */
        setPin(PH_W_LOW_Pin,  true, false);
        setPin(PH_W_HIGH_Pin, true, false);
    }

    /* A DESAT trip can latch between enableGateDriver() and the first PWM edge.
     * Check immediately so we don't run the PI on stale/frozen current. */
    if (GateDriver_IsFault()) {
        uint32_t bdtr = TIM1->BDTR;
        Telemetry::printf("[RES CAL] FAIL: gate-driver fault after enabling PWM | MOE=%lu BIF=%lu BKF=%lu",
                          (bdtr >> 15) & 1UL,
                          (TIM1->SR >> 7) & 1UL,
                          (TIM1->SR >> 6) & 1UL);
        fail("[RES CAL] FAIL: DESAT/gate-driver fault latched after PWM enable");
        return;
    }
    if ((TIM1->BDTR & TIM_BDTR_MOE) == 0U) {
        Telemetry::printf("[RES CAL] FAIL: TIM1 MOE cleared after enabling PWM (break event)");
        fail("[RES CAL] FAIL: TIM1 break event cleared MOE");
        return;
    }
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
            fail("[RES CAL] FAIL: %s inactive current %.3f A exceeds limit (active %.3f A)",
                 pairName(pair),
                 static_cast<double>(i_inactive),
                 static_cast<double>(std::fabs(i_active)));
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
        Telemetry::printf("[RES CAL] %s fit data: "
                          "(V=%.3fV I=%.3fA), "
                          "(V=%.3fV I=%.3fA), "
                          "(V=%.3fV I=%.3fA), "
                          "(V=%.3fV I=%.3fA), "
                          "(V=%.3fV I=%.3fA), "
                          "(V=%.3fV I=%.3fA), "
                          "(V=%.3fV I=%.3fA)",
                          pairName(pair),
                          static_cast<double>(v[0]), static_cast<double>(i[0]),
                          static_cast<double>(v[1]), static_cast<double>(i[1]),
                          static_cast<double>(v[2]), static_cast<double>(i[2]),
                          static_cast<double>(v[3]), static_cast<double>(i[3]),
                          static_cast<double>(v[4]), static_cast<double>(i[4]),
                          static_cast<double>(v[5]), static_cast<double>(i[5]),
                          static_cast<double>(v[6]), static_cast<double>(i[6]));
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
        Telemetry::printf("[RES CAL] %s fit math: sumV=%.3f sumI=%.3f sumVI=%.3f sumII=%.3f denom=%.3f",
                          pairName(pair),
                          static_cast<double>(sum_v),
                          static_cast<double>(sum_i),
                          static_cast<double>(sum_vi),
                          static_cast<double>(sum_ii),
                          static_cast<double>(denom));
    }

    if (std::fabs(denom) < 1e-9f || !std::isfinite(denom)) {
        fail("[RES CAL] FAIL: current did not vary enough between points");
        return;
    }

    const float r_ll = (n * sum_vi - sum_v * sum_i) / denom;
    {
        Telemetry::printf("[RES CAL] %s fit result: R_ll=%.4f mohm",
                          pairName(pair),
                          static_cast<double>(r_ll * 1000.0f));
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

    Telemetry::printf("[RES CAL] %s: R_ll=%.4f mohm  R_phase=%.4f mohm  Imax=%.3f A  Vdc=%.3f V  V_off=%.3f V",
                      pairName(pair),
                      static_cast<double>(r_ll * 1000.0f),
                      static_cast<double>(r_phase * 1000.0f),
                      static_cast<double>(i_active_max),
                      static_cast<double>(vdc_avg),
                      static_cast<double>(v_offset));

    enterState(State::NEXT_PAIR);
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

    Telemetry::printf("[RES CAL] DONE: Rll_uv=%.4f%s Rll_uw=%.4f%s Rll_vw=%.4f%s Rll_avg=%.4f mohm",
                      static_cast<double>(m_results[0] * 2000.0f), m_result_valid[0] ? "" : "(--)",
                      static_cast<double>(m_results[1] * 2000.0f), m_result_valid[1] ? "" : "(--)",
                      static_cast<double>(m_results[2] * 2000.0f), m_result_valid[2] ? "" : "(--)",
                      static_cast<double>(avg_r_ll * 1000.0f));

    Telemetry::log("r_ll_uv", m_results[0] * 2.0f);
    Telemetry::log("r_ll_uw", m_results[1] * 2.0f);
    Telemetry::log("r_ll_vw", m_results[2] * 2.0f);
    Telemetry::log("r_phase_avg", m_average_r_phase);
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
        Telemetry::printf("[RES CAL] FAIL: fault detected");
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

        m_point_index = 0;
        if (m_mode == Mode::VOLTAGE_STEP) {
            configureHardware(m_targets[m_point_index]);
        } else {
            m_pi_integral = 0.0f;
            m_pi_duty = PI_MIN_DUTY;
            m_pi_last_ms = now_ms;
            configureHardware(m_pi_duty);
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
        Telemetry::printf("[RES CAL] FAIL: timeout");
        restoreHardware();
        enterState(State::FAIL);
        return;
    }

    const Pair pair = m_pairs[m_pair_index];

    if (m_state == State::SETTLE) {
        if (elapsed_ms >= SETTLE_TIME_MS) {
            const uint8_t pt = m_point_index;
            resetMeasurementAccumulators(pt);
            if (m_mode == Mode::VOLTAGE_STEP) {
                Telemetry::printf("[RES CAL] %s point %u/%u: target Vll=%.3f V",
                                  pairName(pair), static_cast<unsigned>(pt + 1U),
                                  static_cast<unsigned>(NUM_POINTS),
                                  static_cast<double>(m_targets[pt] * 0.01f * dcLinkVoltageSensor().voltage()));
            } else {
                Telemetry::printf("[RES CAL] %s point %u/%u: target I=%.3f A",
                                  pairName(pair), static_cast<unsigned>(pt + 1U),
                                  static_cast<unsigned>(NUM_POINTS),
                                  static_cast<double>(m_targets[pt]));
            }
            m_update_calls = 0;
            m_sample_calls = 0;
            m_last_rate_log_ms = now_ms;
            m_last_sample_ms = now_ms;
            enterState(State::MEASURE);
        }
        return;
    }

    if (m_state == State::MEASURE) {
        const uint8_t pt = m_point_index;

        /* If the ADC trigger has stopped, the PI will wind up on stale data and
         * we risk a hardware overcurrent.  Abort if no new sample arrives. */
        if (now_ms - m_last_sample_ms > 100U) {
            Telemetry::printf("[RES CAL] FAIL: no new ADC sample for %lu ms (stale current)",
                              static_cast<unsigned long>(now_ms - m_last_sample_ms));
            restoreHardware();
            enterState(State::FAIL);
            return;
        }

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
                Telemetry::printf("[RES CAL] FAIL: overcurrent %.3f A > limit %.3f A",
                                  static_cast<double>(i_active),
                                  static_cast<double>(m_max_current_a));
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
            Telemetry::printf("[RES CAL] %s timing: update=%.3f Hz  sample=%.3f Hz  n_samp=%lu",
                              pairName(pair),
                              static_cast<double>(update_hz),
                              static_cast<double>(sample_hz),
                              static_cast<unsigned long>(m_sample_calls));
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
            Telemetry::printf("[RES CAL] %s point %u/%u done: Vll=%.3f V  Vdc=%.3f V  duty=%.3f %%  Iact=%.3f A  Iinact=%.3f A",
                              pairName(pair), static_cast<unsigned>(pt + 1U),
                              static_cast<unsigned>(NUM_POINTS),
                              static_cast<double>(vll_pt),
                              static_cast<double>(vdc_pt),
                              static_cast<double>(duty_pt),
                              static_cast<double>(iact_pt),
                              static_cast<double>(iinact_pt));

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
        finishPairMeasurement();
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
