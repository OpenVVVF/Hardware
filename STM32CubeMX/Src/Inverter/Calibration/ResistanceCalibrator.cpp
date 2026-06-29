#include "Inverter/Calibration/ResistanceCalibrator.h"

#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Telemetry.h"

#include "main.h"
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
    switch (pair) {
        case ResistanceCalibrator::Pair::UV:
        case ResistanceCalibrator::Pair::UW:
            return iu;
        case ResistanceCalibrator::Pair::VW:
            return iv;
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

    m_bus_pct_a = bus_pct;
    m_bus_pct_b = bus_pct * 0.5f;
    m_max_current_a = (max_current_a < 0.0f) ? 0.0f : max_current_a;
    m_timeout_ms = timeout_ms;
    m_pair_index = 0;
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
    m_sample_count[0] = m_sample_count[1] = 0;
    m_sum_i_active[0] = m_sum_i_active[1] = 0.0f;
    m_sum_i_inactive[0] = m_sum_i_inactive[1] = 0.0f;
    m_sum_vdc[0] = m_sum_vdc[1] = 0.0f;

    enterState(State::ENABLE);

    char buf[16];
    fmtFloat4(buf, sizeof(buf), bus_pct);
    char ibuf[16];
    fmtFloat3(ibuf, sizeof(ibuf), m_max_current_a);
    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] starting (%s) at %s %% bus, max I=%s A",
                  run_all ? "UV/UW/VW" : pairName(pair), buf, ibuf);
    Telemetry::log("print", msg);
    return true;
}

void ResistanceCalibrator::enterState(State state) {
    m_state = state;
    m_state_enter_ms = HAL_GetTick();
}

bool ResistanceCalibrator::enableGateDriver() {
    GateDriver_EnableOutputs();

    bool ready = false;
    bool fault = true;
    for (int i = 0; i < 100; ++i) {
        ready = GateDriver_IsReady();
        fault = GateDriver_IsFault();
        if (ready && !fault) {
            return true;
        }
        HAL_Delay(5);
    }

    Telemetry::log("print", "[RES CAL] FAIL: gate driver not ready or fault latched");
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

    /* Disable all timer outputs while reconfiguring. */
    TIM1->CCER &= ~(TIM_CCER_CC1E | TIM_CCER_CC1NE |
                    TIM_CCER_CC2E | TIM_CCER_CC2NE |
                    TIM_CCER_CC3E | TIM_CCER_CC3NE);

    /* Default all compare registers to 0. */
    TIM1->CCR1 = 0;
    TIM1->CCR2 = 0;
    TIM1->CCR3 = 0;

    /* Set the high-phase compare value. */
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
     * CCER is enabled only for the active phases; for the low phase the timer
     * outputs are ignored because the pins are in GPIO mode. */

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

    /* Enable complementary outputs for the active phases only. */
    switch (pair) {
        case Pair::UV:
            TIM1->CCER |= TIM_CCER_CC1E | TIM_CCER_CC1NE |
                          TIM_CCER_CC2E | TIM_CCER_CC2NE;
            break;
        case Pair::UW:
            TIM1->CCER |= TIM_CCER_CC1E | TIM_CCER_CC1NE |
                          TIM_CCER_CC3E | TIM_CCER_CC3NE;
            break;
        case Pair::VW:
            TIM1->CCER |= TIM_CCER_CC2E | TIM_CCER_CC2NE |
                          TIM_CCER_CC3E | TIM_CCER_CC3NE;
            break;
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

    /* 4. Restore timer registers but leave CCER at 0 (all outputs off). */
    TIM1->CCR1 = m_saved_ccr1;
    TIM1->CCR2 = m_saved_ccr2;
    TIM1->CCR3 = m_saved_ccr3;
    TIM1->PSC  = m_saved_psc;
    TIM1->ARR  = m_saved_arr;
    TIM1->BDTR = (m_saved_bdtr & ~TIM_BDTR_DTG) | (TIM1->BDTR & TIM_BDTR_DTG);
    TIM1->BDTR = m_saved_bdtr;
    TIM1->CCER = 0U; /* explicitly off */

    /* 5. Restore GPIO to original alternate-function modes. */
    GPIOE->MODER = m_saved_gpioe_moder;

    /* 6. Leave the SPWM update interrupt disabled until open-loop starts again. */
    TIM1->DIER &= ~TIM_DIER_UIE;
    HAL_NVIC_DisableIRQ(TIM1_UP_IRQn);
}

void ResistanceCalibrator::finishPairMeasurement() {
    const Pair pair = m_pairs[m_pair_index];

    if (m_sample_count[0] < MIN_SAMPLES || m_sample_count[1] < MIN_SAMPLES) {
        enterState(State::FAIL);
        Telemetry::log("print", "[RES CAL] FAIL: not enough samples");
        return;
    }

    const float i_a = m_sum_i_active[0] / static_cast<float>(m_sample_count[0]);
    const float i_b = m_sum_i_active[1] / static_cast<float>(m_sample_count[1]);
    const float i_inactive_a = std::fabs(
        m_sum_i_inactive[0] / static_cast<float>(m_sample_count[0]));
    const float i_inactive_b = std::fabs(
        m_sum_i_inactive[1] / static_cast<float>(m_sample_count[1]));
    const float vdc_a = m_sum_vdc[0] / static_cast<float>(m_sample_count[0]);
    const float vdc_b = m_sum_vdc[1] / static_cast<float>(m_sample_count[1]);

    /* The inactive (high-Z) phase should carry essentially zero current. */
    const float max_inactive_a = std::max(
        MAX_INACTIVE_CURRENT_MIN_A, std::fabs(i_a) * MAX_INACTIVE_CURRENT_RATIO);
    const float max_inactive_b = std::max(
        MAX_INACTIVE_CURRENT_MIN_A, std::fabs(i_b) * MAX_INACTIVE_CURRENT_RATIO);

    if (i_inactive_a > max_inactive_a || i_inactive_b > max_inactive_b) {
        enterState(State::FAIL);
        char abuf[16], ibuf[16];
        fmtFloat3(abuf, sizeof(abuf), std::max(std::fabs(i_a), std::fabs(i_b)));
        fmtFloat3(ibuf, sizeof(ibuf), std::max(i_inactive_a, i_inactive_b));
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "[RES CAL] FAIL: %s inactive current %s A exceeds limit (active %s A)",
                      pairName(pair), ibuf, abuf);
        Telemetry::log("print", msg);
        return;
    }

    /* Line-to-line voltage at point A and B. */
    const float v_ll_a = (m_bus_pct_a / 100.0f) * vdc_a;
    const float v_ll_b = (m_bus_pct_b / 100.0f) * vdc_b;
    const float delta_v = v_ll_a - v_ll_b;
    const float delta_i = i_a - i_b;
    const float abs_delta_i = std::fabs(delta_i);

    if (abs_delta_i < 0.01f) {
        enterState(State::FAIL);
        char ia_buf[16], ib_buf[16];
        fmtFloat3(ia_buf, sizeof(ia_buf), i_a);
        fmtFloat3(ib_buf, sizeof(ib_buf), i_b);
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "[RES CAL] FAIL: %s current did not change (Ia=%s A  Ib=%s A)",
                      pairName(pair), ia_buf, ib_buf);
        Telemetry::log("print", msg);
        return;
    }

    const float r_ll = std::fabs(delta_v) / abs_delta_i;
    if (r_ll <= 0.0f || !std::isfinite(r_ll)) {
        enterState(State::FAIL);
        Telemetry::log("print", "[RES CAL] FAIL: computed resistance is non-positive; increase bus_pct");
        return;
    }

    const float r_phase = r_ll * 0.5f;

    const int idx = pairIndex(pair);
    m_results[idx] = r_phase;
    m_result_valid[idx] = true;

    char rll_buf[16], rph_buf[16];
    fmtFloat4(rll_buf, sizeof(rll_buf), r_ll * 1000.0f);
    fmtFloat4(rph_buf, sizeof(rph_buf), r_phase * 1000.0f);
    char ia_buf[16], ib_buf[16];
    fmtFloat3(ia_buf, sizeof(ia_buf), i_a);
    fmtFloat3(ib_buf, sizeof(ib_buf), i_b);
    char vbuf[16];
    fmtFloat3(vbuf, sizeof(vbuf), (vdc_a + vdc_b) * 0.5f);
    char msg[160];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] %s: R_ll=%s mohm  R_phase=%s mohm  I=%s/%s A  Vdc=%s V",
                  pairName(pair), rll_buf, rph_buf, ia_buf, ib_buf, vbuf);
    Telemetry::log("print", msg);

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

void ResistanceCalibrator::update() {
    if (m_state == State::IDLE || m_state == State::DONE ||
        m_state == State::FAIL) {
        return;
    }

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

        configureHardware(m_bus_pct_a);
        enterState(State::SETTLE_A);
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

    if (m_state == State::SETTLE_A || m_state == State::SETTLE_B) {
        if (elapsed_ms >= SETTLE_TIME_MS) {
            const int pt = (m_state == State::SETTLE_A) ? 0 : 1;
            m_sample_count[pt] = 0;
            m_sum_i_active[pt] = 0.0f;
            m_sum_i_inactive[pt] = 0.0f;
            m_sum_vdc[pt] = 0.0f;
            char vbuf[16];
            fmtFloat4(vbuf, sizeof(vbuf),
                      (pt == 0 ? m_bus_pct_a : m_bus_pct_b) * 0.01f * dcLinkVoltageSensor().voltage());
            char msg[80];
            std::snprintf(msg, sizeof(msg),
                          "[RES CAL] %s point %c: target Vll=%s V",
                          pairName(pair), (pt == 0) ? 'A' : 'B', vbuf);
            Telemetry::log("print", msg);
            enterState((m_state == State::SETTLE_A) ? State::MEASURE_A : State::MEASURE_B);
        }
        return;
    }

    if (m_state == State::MEASURE_A || m_state == State::MEASURE_B) {
        const int pt = (m_state == State::MEASURE_A) ? 0 : 1;

        float iu, iv, iw;
        if (phaseCurrentADC().sample(iu, iv, iw)) {
            const float i_active = pairCurrentActive(iu, iv, iw, pair);
            m_sum_i_active[pt] += i_active;
            m_sum_i_inactive[pt] += pairCurrentInactive(iu, iv, iw, pair);
            m_sum_vdc[pt] += dcLinkVoltageSensor().voltage();
            ++m_sample_count[pt];

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

        if (elapsed_ms >= MEASURE_TIME_MS && m_sample_count[pt] >= MIN_SAMPLES) {
            if (m_state == State::MEASURE_A) {
                configureHardware(m_bus_pct_b);
                enterState(State::SETTLE_B);
            } else {
                finishPairMeasurement();
            }
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
        configureHardware(m_bus_pct_a);
        enterState(State::SETTLE_A);
        return;
    }
}

} // namespace Inverter
