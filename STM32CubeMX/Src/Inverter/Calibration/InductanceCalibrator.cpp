#include "Inverter/Calibration/InductanceCalibrator.h"
#include "Inverter/Calibration/MotorCalibration.h"
#include "Inverter/Calibration/PoleCalibrator.h"
#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Calibration/ResistanceCalibrator.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Control/FocControlManager.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Telemetry.h"

#include "main.h"

#include <cstdarg>
#include <cmath>
#include <cstdio>

namespace Inverter {

namespace {

InductanceCalibrator s_instance;

/* IsrTrampoline: FocControlManager's sample callback signature has no
 * knowledge of the calibrator; route through the singleton. */
void focSampleTrampoline(float id, float iq, float vd, float vq, void*) {
    s_instance.onFocSample(id, iq, vd, vq);
}

constexpr uint32_t BIAS_SETTLE_MS    = 150U;
constexpr uint32_t LQ_SETTLE_MS      = 400U;
constexpr float    LQ_BIAS_FRAC      = 0.4f;  /* q bias ladder = 0.4x the d ladder */
constexpr int      MEASURE_CYCLES    = 12;  /* AC cycles accumulated per point */
constexpr int      SKIP_CYCLES       = 2;   /* leading cycles discarded (ramp) */

} // namespace

InductanceCalibrator& InductanceCalibrator::instance() {
    return s_instance;
}

InductanceCalibrator& inductanceCalibrator() {
    return InductanceCalibrator::instance();
}

void InductanceCalibrator::restoreGains() {
    if (m_orig_kp > 0.0f) {
        focControlManager().setKp(m_orig_kp);
        focControlManager().setKi(m_orig_ki);
        m_orig_kp = 0.0f;
    }
}

void InductanceCalibrator::restoreSwitchingFrequency() {
    if (m_saved_switching_hz > 0.0f) {
        PWM_SetFrequency(static_cast<uint32_t>(m_saved_switching_hz + 0.5f));
        m_saved_switching_hz = 0.0f;
    }
}

bool InductanceCalibrator::isActive() const {
    return m_state != State::IDLE && m_state != State::DONE && m_state != State::FAIL;
}

const char* InductanceCalibrator::stateName() const {
    switch (m_state) {
        case State::IDLE:       return "IDLE";
        case State::LD_SETTLE:  return "LD_SETTLE";
        case State::LD_MEASURE: return "LD_MEASURE";
        case State::LQ_SETTLE:  return "LQ_SETTLE";
        case State::LQ_MEASURE: return "LQ_MEASURE";
        case State::FINISH:     return "FINISH";
        case State::DONE:       return "DONE";
        case State::FAIL:       return "FAIL";
    }
    return "?";
}

float InductanceCalibrator::biasLdPoint(int i) const {
    return (i >= 0 && i < m_n_points && i < MAX_POINTS) ? m_bias_ld[i] : 0.0f;
}
float InductanceCalibrator::biasLqPoint(int i) const {
    return (i >= 0 && i < m_n_points && i < MAX_POINTS) ? m_bias_lq[i] : 0.0f;
}
float InductanceCalibrator::ldPoint(int i) const {
    return (i >= 0 && i < m_n_points && i < MAX_POINTS) ? m_ld[i] : 0.0f;
}
float InductanceCalibrator::lqPoint(int i) const {
    return (i >= 0 && i < m_n_points && i < MAX_POINTS) ? m_lq[i] : 0.0f;
}

void InductanceCalibrator::fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(m_fail_reason, sizeof(m_fail_reason), fmt, ap);
    va_end(ap);
    Telemetry::printf("[CAL] IND: FAIL: %s", m_fail_reason);

    /* Back off to zero current and release the power stage. */
    if (focControlManager().isRunning()) {
        focControlManager().setId(0.0f);
        focControlManager().setIq(0.0f);
    }
    focControlManager().setSampleCallback(nullptr, nullptr);
    focControlManager().stop();
    restoreGains();
    restoreSwitchingFrequency();
    enterState(State::FAIL);
}

void InductanceCalibrator::enterState(State s) {
    m_state = s;
    m_state_enter_ms = HAL_GetTick();
}

bool InductanceCalibrator::start(float max_current_a, float ac_current_a,
                                 float inj_freq_hz) {
    if (isActive()) {
        Telemetry::printf("[CAL] IND: already running");
        return false;
    }
    if (openLoopController().isRunning() || focControlManager().isRunning()) {
        Telemetry::printf("[CAL] IND: stop the motor first");
        return false;
    }
    /* The coordinator may be active (it calls us for its INDUCTANCE stage);
     * the individual calibrators drive the power stage directly and would
     * collide. */
    if (poleCalibrator().isActive() || encoderOffsetCalibrator().isActive() ||
        resistanceCalibrator().isActive()) {
        Telemetry::printf("[CAL] IND: another calibration is active");
        return false;
    }
    const MotorCalibration& mc = motorCalibration();
    if (!mc.valid || mc.r_phase_avg <= 0.0f) {
        Telemetry::printf("[CAL] IND: need a valid motor calibration first (run motorcal)");
        return false;
    }
    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::printf("[CAL] IND: active Critical/High faults, cannot start");
        return false;
    }
    if (dcLinkVoltageSensor().voltage() < 10.0f) {
        Telemetry::printf("[CAL] IND: DC bus too low (%.1f V)",
                          static_cast<double>(dcLinkVoltageSensor().voltage()));
        return false;
    }
    if (max_current_a < 5.0f || max_current_a > 200.0f ||
        ac_current_a <= 0.0f || ac_current_a > 10.0f ||
        inj_freq_hz < 10.0f || inj_freq_hz > 400.0f) {
        Telemetry::printf("[CAL] IND: bad parameters");
        return false;
    }

    m_max_current_a = max_current_a;
    m_ac_current_a = ac_current_a;
    m_inj_freq_hz = inj_freq_hz;

    m_ld0 = m_lq0 = 0.0f;
    m_n_points = 0;
    for (int i = 0; i < MAX_POINTS; ++i) {
        m_bias_ld[i] = m_bias_lq[i] = m_ld[i] = m_lq[i] = 0.0f;
    }
    m_fail_reason[0] = '\0';
    m_retries_left = 2;

    /* Pin a known switching frequency for the measurement (the runtime
     * frequency is user-defined and may be anything); restored on exit. */
    m_saved_switching_hz = PWM_GetFrequency();
    PWM_SetFrequency(static_cast<uint32_t>(CAL_SWITCHING_HZ));

    /* Injection oscillator increment, using the real control rate. */
    float fs = PWM_GetUpdateFrequency();
    if (fs <= 0.0f) fs = 2500.0f;
    m_fs = fs;
    const float step = 2.0f * 3.14159265358979323846f * m_inj_freq_hz / fs;
    m_osc_dc = std::cos(step);
    m_osc_ds = std::sin(step);
    const uint32_t samples_per_cycle =
        static_cast<uint32_t>(fs / m_inj_freq_hz + 0.5f);
    m_skip_samples = samples_per_cycle * static_cast<uint32_t>(SKIP_CYCLES);
    m_target_samples = samples_per_cycle * static_cast<uint32_t>(MEASURE_CYCLES - SKIP_CYCLES);

    m_abort_requested = false;

    /* The calibrator runs its own stage of motorcal, so the "calibration
     * active" guard must be bypassed (the coordinator is waiting on us). */
    if (!focControlManager().start(0.0f, 0.0f, true)) {
        Telemetry::printf("[CAL] IND: failed to start FOC");
        return false;
    }
    focControlManager().setSampleCallback(focSampleTrampoline, nullptr);
    m_orig_kp = 0.0f;

    Telemetry::printf("[CAL] IND: biased-AC injection: max=%.1f A ac=%.2f A f=%.1f Hz "
                      "R=%.4f ohm",
                      static_cast<double>(m_max_current_a),
                      static_cast<double>(m_ac_current_a),
                      static_cast<double>(m_inj_freq_hz),
                      static_cast<double>(mc.r_phase_avg));

    /* No alignment phase: in the closed-loop encoder frame, constant d
     * current makes no torque (Ld needs no equilibrium) and constant q
     * current makes constant torque (no equilibrium exists for Lq anyway).
     * Jump straight into the first Ld point; its ramp brings the current up
     * gently. */
    beginLdPoint(0);
    return true;
}

void InductanceCalibrator::stop() {
    if (!isActive()) {
        return;
    }
    focControlManager().setSampleCallback(nullptr, nullptr);
    focControlManager().stop();
    restoreGains();
    restoreSwitchingFrequency();
    Telemetry::printf("[CAL] IND: stopped");
    enterState(State::IDLE);
}

void InductanceCalibrator::resetGoertzel() {
    m_osc_c = 1.0f;
    m_osc_s = 0.0f;
    m_osc_n = 0;
    m_i_re = m_i_im = 0.0;
    m_v_re = m_v_im = 0.0;
    m_n = 0;
    m_peak_abs_i = 0.0f;
}

void InductanceCalibrator::armSetpoints(const MeasurePoint& mp, bool ac_on) {
    m_mp = mp;
    m_settle_n = 0;
    if (!ac_on) {
        /* Bias only, AC off. */
        if (mp.axis_q) {
            focControlManager().setId(0.0f);
            focControlManager().setIq(mp.bias_a);
        } else {
            focControlManager().setId(mp.bias_a);
            focControlManager().setIq(0.0f);
        }
    }
    resetGoertzel();
}

bool InductanceCalibrator::beginLdPoint(int index) {
    __disable_irq();
    m_point_index = index;
    /* The 0-bias point is unmeasurable with a free rotor (the unsaturated
     * incremental inductance is enormous and the weak alignment lets rotor
     * dynamics pollute the response), so the curve starts at 15% of max. */
    const float frac = 0.15f + 0.85f * static_cast<float>(index) /
                       static_cast<float>(MAX_POINTS - 1);
    armSetpoints(MeasurePoint{frac * m_max_current_a, false, -1, BIAS_SETTLE_MS}, false);
    enterState(State::LD_SETTLE);
    __enable_irq();
    return true;
}

bool InductanceCalibrator::beginLqHalf(int index, bool negative_half) {
    __disable_irq();
    m_point_index = index;
    m_lq_negative_half = negative_half;
    /* q bias follows the same fraction ladder as Ld, scaled by LQ_BIAS_FRAC:
     * the d-axis alignment can only hold so much q torque.  Single polarity
     * keeps the rotor at one equilibrium instead of swinging through the
     * alignment at every polarity flip. */
    const float frac = (0.15f + 0.85f * static_cast<float>(index) /
                        static_cast<float>(MAX_POINTS - 1)) * LQ_BIAS_FRAC;
    float bias = frac * m_max_current_a;
    if (negative_half) bias = -bias;
    armSetpoints(MeasurePoint{bias, true, index, BIAS_SETTLE_MS}, false);
    enterState(State::LQ_SETTLE);
    __enable_irq();
    return true;
}

float InductanceCalibrator::computeInductance(float i_amp, float v_amp) const {
    if (i_amp <= 1.0e-6f) return 0.0f;
    const float z = v_amp / i_amp;
    const float r = motorCalibration().r_phase_avg;
    const float omega = 2.0f * 3.14159265358979323846f * m_inj_freq_hz;
    const float z2r2 = z * z - r * r;
    if (z2r2 <= 0.0f) return 0.0f;
    return std::sqrt(z2r2) / omega;
}

void InductanceCalibrator::finishMeasure() {
    if (m_n == 0) {
        fail("no samples accumulated");
        return;
    }
    const double inv = 2.0 / static_cast<double>(m_n);
    const float i_amp = static_cast<float>(inv * std::sqrt(m_i_re * m_i_re + m_i_im * m_i_im));
    const float v_amp = static_cast<float>(inv * std::sqrt(m_v_re * m_v_re + m_v_im * m_v_im));
    const int k = m_point_index;

    /* Amplitude auto-retry: if the loop tracked the command too weakly
     * (small-signal distortion dominates at tiny amplitudes), double the AC
     * amplitude and repeat the point. */
    if (i_amp < 1.5f && m_ac_current_a < 10.0f && m_retries_left > 0) {
        --m_retries_left;
        m_ac_current_a *= 2.0f;
        if (m_ac_current_a > 10.0f) m_ac_current_a = 10.0f;
        Telemetry::printf("[CAL] IND: weak response (I=%.2f A); retry with ac=%.1f A",
                          static_cast<double>(i_amp), static_cast<double>(m_ac_current_a));
        if (m_mp.axis_q) {
            beginLqHalf(k, m_lq_negative_half);
        } else {
            beginLdPoint(k);
        }
        return;
    }

    const float l = computeInductance(i_amp, v_amp);

    /* Quality gate: below ~1.5 A of actual AC the measurement is dominated
     * by small-signal distortion.  Skip the point rather than store junk. */
    const bool weak = (i_amp < 1.5f);

    if (!m_mp.axis_q) {
        if (weak) {
            Telemetry::printf("[CAL] IND: Ld(%5.1f A) skipped (I=%.2f A too weak)",
                              static_cast<double>(m_mp.bias_a), static_cast<double>(i_amp));
        } else {
            if (l <= 0.0f) {
                fail("Ld point %d: nonphysical result (I=%.2f A V=%.2f V)",
                     k, static_cast<double>(i_amp), static_cast<double>(v_amp));
                return;
            }
            m_bias_ld[k] = m_mp.bias_a;
            m_ld[k] = l;
            Telemetry::printf("[CAL] IND: Ld(%5.1f A) = %8.1f uH  (I=%.2f V=%.2f)",
                              static_cast<double>(m_mp.bias_a), static_cast<double>(l * 1.0e6),
                              static_cast<double>(i_amp), static_cast<double>(v_amp));
        }
        m_retries_left = 2;
        if (k + 1 < MAX_POINTS) {
            beginLdPoint(k + 1);
        } else {
            /* d curve complete; Lq starts at the smallest q bias. */
            beginLqHalf(1, false);
        }
        return;
    }

    /* q-axis: one measurement per bias level (single polarity). */
    if (weak) {
        Telemetry::printf("[CAL] IND: Lq(%5.1f A) too weak (I=%.2f A); skipping point",
                          static_cast<double>(m_mp.bias_a), static_cast<double>(i_amp));
        if (k + 1 < MAX_POINTS) {
            beginLqHalf(k + 1, false);
        } else {
            enterState(State::FINISH);
        }
        return;
    }

    Telemetry::printf("[CAL] IND: Lq(%5.1f A) = %8.1f uH  (I=%.2f V=%.2f)",
                      static_cast<double>(m_mp.bias_a), static_cast<double>(l * 1.0e6),
                      static_cast<double>(i_amp), static_cast<double>(v_amp));

    if (l <= 0.0f) {
        Telemetry::printf("[CAL] IND: Lq point %d dropped (nonphysical)", k);
    } else {
        m_bias_lq[k] = m_mp.bias_a;
        m_lq[k] = l;
    }
    m_retries_left = 2;
    if (k + 1 < MAX_POINTS) {
        beginLqHalf(k + 1, false);
    } else {
        enterState(State::FINISH);
    }
}

void InductanceCalibrator::onFocSample(float id, float iq, float vd, float vq) {
    const bool measuring = (m_state == State::LD_MEASURE || m_state == State::LQ_MEASURE);
    const bool armed = measuring || (m_state == State::LD_SETTLE || m_state == State::LQ_SETTLE);
    if (!armed || m_abort_requested) {
        return;
    }

    /* Drive the setpoints.  During settle the bias ramps in over the settle
     * window so the rotor follows quasi-statically instead of ringing from
     * a torque step; the AC rides on top during measurement. */
    float bias = m_mp.bias_a;
    if (!measuring) {
        const uint32_t ramp_n = static_cast<uint32_t>(m_fs * (m_mp.settle_ms * 1.0e-3f));
        if (ramp_n > 0U && m_settle_n < ramp_n) {
            bias *= static_cast<float>(m_settle_n) / static_cast<float>(ramp_n);
        }
        ++m_settle_n;
    }
    const float ac = m_ac_current_a * m_osc_s;
    if (m_mp.axis_q) {
        /* Lq: free-spinning rotor, no d-hold current. */
        focControlManager().setId(0.0f);
        focControlManager().setIq(measuring ? (bias + ac) : bias);
    } else {
        focControlManager().setId(measuring ? (bias + ac) : bias);
        focControlManager().setIq(0.0f);
    }

    /* Safety: instantaneous overcurrent guard on the regulated dq currents.
     * fail() must not run in ISR context, so hand off to the main loop. */
    const float abs_i = std::fabs(m_mp.axis_q ? iq : id);
    if (abs_i > m_peak_abs_i) m_peak_abs_i = abs_i;
    if (abs_i > m_overcurrent_a) {
        m_abort_current_a = abs_i;
        m_abort_requested = true;
        focControlManager().setId(0.0f);
        focControlManager().setIq(0.0f);
        return;
    }

    if (!measuring) {
        return;
    }

    /* Goertzel accumulation on the measured channel, skipping the ramp. */
    const float i_sig = m_mp.axis_q ? iq : id;
    const float v_sig = m_mp.axis_q ? vq : vd;

    if (m_osc_n >= m_skip_samples) {
        m_i_re += static_cast<double>(i_sig) * m_osc_c;
        m_i_im -= static_cast<double>(i_sig) * m_osc_s;
        m_v_re += static_cast<double>(v_sig) * m_osc_c;
        m_v_im -= static_cast<double>(v_sig) * m_osc_s;
        ++m_n;
    }

    /* Advance the oscillator (rotation, renormalized periodically). */
    const float c = m_osc_c * m_osc_dc - m_osc_s * m_osc_ds;
    const float s = m_osc_c * m_osc_ds + m_osc_s * m_osc_dc;
    m_osc_c = c;
    m_osc_s = s;
    if ((++m_osc_n & 0x3FFU) == 0U) {
        const float mag = std::sqrt(c * c + s * s);
        if (mag > 0.0f) {
            m_osc_c /= mag;
            m_osc_s /= mag;
        }
    }
}

void InductanceCalibrator::update() {
    if (!isActive()) {
        return;
    }

    /* Overcurrent (or other ISR-side) abort handoff. */
    if (m_abort_requested) {
        m_abort_requested = false;
        fail("overcurrent %.1f A > %.1f A",
             static_cast<double>(m_abort_current_a),
             static_cast<double>(m_overcurrent_a));
        return;
    }

    const uint32_t now = HAL_GetTick();
    const uint32_t in_state = now - m_state_enter_ms;

    switch (m_state) {
        case State::LD_SETTLE:
        case State::LQ_SETTLE: {
            /* Fixed quiet window while the bias ramps in; no stillness or
             * excursion checks — the rotor's motion is irrelevant in the
             * closed-loop frame (Ld makes no torque; Lq is meant to spin). */
            if (in_state < m_mp.settle_ms) {
                return;
            }
            __disable_irq();
            resetGoertzel();
            enterState(m_mp.axis_q ? State::LQ_MEASURE : State::LD_MEASURE);
            __enable_irq();
            return;
        }

        case State::LD_MEASURE:
        case State::LQ_MEASURE: {
            if (m_n < m_target_samples) {
                /* Watchdog in case the ISR stops feeding us. */
                if (in_state > 4000U) {
                    fail("measurement stalled (%lu/%lu samples)",
                         static_cast<unsigned long>(m_n),
                         static_cast<unsigned long>(m_target_samples));
                }
                return;
            }
            finishMeasure();
            return;
        }

        case State::FINISH: {
            /* Headlines: the second bias point when available — still low
             * bias (pre-saturation region) but with much better SNR than the
             * first point.  Falls back to the first clean point. */
            m_ld0 = 0.0f;
            m_lq0 = 0.0f;
            m_n_points = 0;
            for (int i = 0; i < MAX_POINTS; ++i) {
                if (m_ld[i] > 0.0f || m_lq[i] > 0.0f) {
                    m_n_points = i + 1;
                }
            }
            int first_ld = -1, first_lq = -1;
            for (int i = 0; i < m_n_points; ++i) {
                if (first_ld < 0 && m_ld[i] > 0.0f) first_ld = i;
                if (first_lq < 0 && m_lq[i] > 0.0f) first_lq = i;
            }
            if (first_ld >= 0) {
                m_ld0 = m_ld[first_ld];
                if (first_ld + 1 < m_n_points && m_ld[first_ld + 1] > 0.0f) {
                    m_ld0 = m_ld[first_ld + 1];
                }
            }
            if (first_lq >= 0) {
                m_lq0 = m_lq[first_lq];
                if (first_lq + 1 < m_n_points && m_lq[first_lq + 1] > 0.0f) {
                    m_lq0 = m_lq[first_lq + 1];
                }
            }

            /* Publish into the runtime calibration so FOC and the FRAM store
             * see the fresh inductances. */
            if (m_ld0 > 0.0f || m_lq0 > 0.0f) {
                MotorCalibration& mc = motorCalibration();
                if (m_ld0 > 0.0f) mc.ld_henry = m_ld0;
                if (m_lq0 > 0.0f) mc.lq_henry = m_lq0;
            }

            focControlManager().setSampleCallback(nullptr, nullptr);
            focControlManager().stop();
            restoreGains();
            restoreSwitchingFrequency();

            Telemetry::printf("[CAL] IND: ========================================");
            Telemetry::printf("[CAL] IND: INDUCTANCE COMPLETE");
            Telemetry::printf("[CAL] IND:   Ld(0) = %.1f uH   Lq(0) = %.1f uH",
                              static_cast<double>(m_ld0 * 1.0e6),
                              static_cast<double>(m_lq0 * 1.0e6));
            for (int i = 0; i < m_n_points; ++i) {
                if (m_ld[i] > 0.0f) {
                    Telemetry::printf("[CAL] IND:   Ld(%5.1f A) = %7.1f uH",
                                      static_cast<double>(m_bias_ld[i]),
                                      static_cast<double>(m_ld[i] * 1.0e6));
                }
                if (m_lq[i] > 0.0f) {
                    Telemetry::printf("[CAL] IND:   Lq(%5.1f A) = %7.1f uH",
                                      static_cast<double>(m_bias_lq[i]),
                                      static_cast<double>(m_lq[i] * 1.0e6));
                }
            }
            Telemetry::printf("[CAL] IND: ========================================");

            Telemetry::log("ind_ld_uh", m_ld0 * 1.0e6f);
            Telemetry::log("ind_lq_uh", m_lq0 * 1.0e6f);
            enterState(State::DONE);
            return;
        }

        default:
            return;
    }
}

} // namespace Inverter
