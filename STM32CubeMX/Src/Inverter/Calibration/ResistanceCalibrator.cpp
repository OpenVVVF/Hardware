#include "Inverter/Calibration/ResistanceCalibrator.h"

#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
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
    m_original_freq_hz = PWM_GetFrequency();
    if (m_original_freq_hz == 0) {
        m_original_freq_hz = 10000U;
    }
    m_pair_index = 0;
    m_num_pairs = run_all ? 3U : 1U;
    m_pairs[0] = pair;
    if (run_all) {
        /* Fixed order: requested pair, then the other two. */
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

void ResistanceCalibrator::configurePair(Pair pair, float bus_pct) {
    /* Drive all three phases.  The inactive phase is held at 50 % (neutral)
     * so the gate-driver inputs remain actively driven; this avoids the
     * floating-input / spurious-conduction issues seen with a true high-Z
     * phase on this gate driver. */
    PWM_StartPhase(0);
    PWM_StartPhase(1);
    PWM_StartPhase(2);

    const float d = bus_pct * 0.5f; /* duty deviation in percent */
    float du = 50.0f;
    float dv = 50.0f;
    float dw = 50.0f;

    switch (pair) {
        case Pair::UV:
            du = 50.0f + d;
            dv = 50.0f - d;
            dw = 50.0f;
            break;
        case Pair::UW:
            du = 50.0f + d;
            dw = 50.0f - d;
            dv = 50.0f;
            break;
        case Pair::VW:
            dv = 50.0f + d;
            dw = 50.0f - d;
            du = 50.0f;
            break;
    }

    PWM_SetThreePhaseDuty(du, dv, dw);
}

void ResistanceCalibrator::cleanup() {
    /* Park all phases at 50 % before disabling gate driver. */
    PWM_StartPhase(0);
    PWM_StartPhase(1);
    PWM_StartPhase(2);
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);

    openLoopController().stop();

    /* Restore the original switching frequency. */
    PWM_SetFrequency(m_original_freq_hz);
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

    /* The inactive (neutral) phase should carry essentially zero current. */
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

    const float delta_i = i_a - i_b;
    if (std::fabs(delta_i) < 0.01f) {
        enterState(State::FAIL);
        Telemetry::log("print", "[RES CAL] FAIL: current did not change between points");
        return;
    }

    /* Line-to-line voltage at point A and B. */
    const float v_ll_a = (m_bus_pct_a / 100.0f) * vdc_a;
    const float v_ll_b = (m_bus_pct_b / 100.0f) * vdc_b;
    const float delta_v = v_ll_a - v_ll_b;

    const float r_ll = delta_v / delta_i;
    if (r_ll <= 0.0f || !std::isfinite(r_ll)) {
        enterState(State::FAIL);
        Telemetry::log("print", "[RES CAL] FAIL: computed resistance is non-positive; increase bus_pct");
        return;
    }

    const float r_phase = r_ll * 0.5f;

    const int idx = pairIndex(pair);
    m_results[idx] = r_phase;
    m_result_valid[idx] = true;

    char rbuf[16];
    fmtFloat4(rbuf, sizeof(rbuf), r_phase * 1000.0f);
    char ia_buf[16], ib_buf[16];
    fmtFloat3(ia_buf, sizeof(ia_buf), i_a);
    fmtFloat3(ib_buf, sizeof(ib_buf), i_b);
    char vbuf[16];
    fmtFloat3(vbuf, sizeof(vbuf), (vdc_a + vdc_b) * 0.5f);
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] %s: R_phase=%s mohm  I=%s/%s A  Vdc=%s V",
                  pairName(pair), rbuf, ia_buf, ib_buf, vbuf);
    Telemetry::log("print", msg);

    enterState(State::NEXT_PAIR);
}

void ResistanceCalibrator::reportResults() {
    float sum = 0.0f;
    uint32_t count = 0;
    for (int i = 0; i < 3; ++i) {
        if (m_result_valid[i]) {
            sum += m_results[i];
            ++count;
        }
    }
    m_average_r_phase = (count > 0) ? (sum / static_cast<float>(count)) : 0.0f;

    char uv[16], uw[16], vw[16], avg[16];
    fmtFloat4(uv, sizeof(uv), m_results[0] * 1000.0f);
    fmtFloat4(uw, sizeof(uw), m_results[1] * 1000.0f);
    fmtFloat4(vw, sizeof(vw), m_results[2] * 1000.0f);
    fmtFloat4(avg, sizeof(avg), m_average_r_phase * 1000.0f);

    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "[RES CAL] DONE: R_uv=%s R_uw=%s R_vw=%s avg=%s mohm",
                  m_result_valid[0] ? uv : "--",
                  m_result_valid[1] ? uw : "--",
                  m_result_valid[2] ? vw : "--",
                  avg);
    Telemetry::log("print", msg);

    Telemetry::log("r_uv", m_results[0]);
    Telemetry::log("r_uw", m_results[1]);
    Telemetry::log("r_vw", m_results[2]);
    Telemetry::log("r_avg", m_average_r_phase);
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
        cleanup();
        enterState(State::FAIL);
        return;
    }

    if (m_state == State::ENABLE) {
        /* Drop switching frequency during calibration to double the duty
         * resolution, which matters when applying sub-1 % bus voltages. */
        PWM_SetFrequency(CAL_SWITCHING_FREQ_HZ);

        /* Bring the gate driver up using the open-loop controller at zero
         * frequency/modulation, then stop the SPWM ISR so our DC vector is
         * not overwritten. */
        if (!openLoopController().start(0.0f, 0.0f)) {
            Telemetry::log("print", "[RES CAL] FAIL: could not enable gate driver");
            enterState(State::FAIL);
            return;
        }
        PWM_StopSPWM();
        configurePair(m_pairs[0], m_bus_pct_a);
        enterState(State::SETTLE_A);
        return;
    }

    const uint32_t elapsed_ms = now_ms - m_state_enter_ms;
    if (elapsed_ms > m_timeout_ms) {
        Telemetry::log("print", "[RES CAL] FAIL: timeout");
        cleanup();
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

            /* Immediate abort if a single sample exceeds the configured max.
             * This protects the power supply / motor from a shorted phase or
             * an accidentally excessive bus_pct. */
            if (m_max_current_a > 0.0f && std::fabs(i_active) > m_max_current_a) {
                char abuf[16], mbuf[16];
                fmtFloat3(abuf, sizeof(abuf), i_active);
                fmtFloat3(mbuf, sizeof(mbuf), m_max_current_a);
                char msg[96];
                std::snprintf(msg, sizeof(msg),
                              "[RES CAL] FAIL: overcurrent %s A > limit %s A",
                              abuf, mbuf);
                Telemetry::log("print", msg);
                cleanup();
                enterState(State::FAIL);
                return;
            }
        }

        if (elapsed_ms >= MEASURE_TIME_MS && m_sample_count[pt] >= MIN_SAMPLES) {
            if (m_state == State::MEASURE_A) {
                configurePair(pair, m_bus_pct_b);
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
            cleanup();
            reportResults();
            enterState(State::DONE);
            return;
        }
        configurePair(m_pairs[m_pair_index], m_bus_pct_a);
        enterState(State::SETTLE_A);
        return;
    }
}

} // namespace Inverter
