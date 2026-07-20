#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Offline Ld/Lq estimation via DC-biased AC signal injection.
 *
 * Implements the biased-AC standstill method (see Rafaq & Jung, "A
 * Comprehensive Review of State-of-the-Art Parameter Estimation Techniques
 * for PMSMs", IEEE TII 2020, Section III-B, and references [70]-[72]):
 *
 *   - The rotor is held at standstill by a d-axis alignment current (no
 *     torque is produced when the current frame is aligned with the rotor
 *     d-axis, so no shaft clamp is needed).
 *   - A small AC current is superimposed on a DC bias that sets the magnetic
 *     operating point; the differential (incremental) inductance at that
 *     bias is extracted from the AC voltage/current ratio at the injection
 *     frequency:  L = sqrt(|Z|^2 - R^2) / omega,  |Z| = |V_ac| / |I_ac|.
 *   - The DC bias is swept to produce an Ld(Id) saturation curve.  Lq is
 *     measured at zero q-bias (free rotor) and optionally at alternating
 *     +/-Iq bias levels with a rotor-movement guard.
 *
 * Everything runs in closed-loop current control (FOC): the current is the
 * controlled variable, so VSI nonlinearities do not pollute the estimate.
 * The AC component of the measured dq currents and commanded dq voltages is
 * extracted with a Goertzel (single-bin DFT) accumulator running in the FOC
 * PWM ISR via FocControlManager's sample callback.
 */
class InductanceCalibrator {
public:
    static constexpr int MAX_POINTS = 8;

    /**
     * @brief Start the calibration.
     *
     * Requires: valid motor calibration (poles/encoder offset/sign and phase
     * resistance), DC bus present, no active Critical/High faults, motor
     * stopped, FOC not running.
     *
     * @param max_current_a  Highest DC bias point [A] (curve endpoint).
     * @param ac_current_a   AC injection amplitude [A].
     * @param inj_freq_hz    Injection frequency [Hz] (kept << PWM frequency,
     *                       above rotor-following bandwidth).
     */
    bool start(float max_current_a = 30.0f, float ac_current_a = 3.0f,
               float inj_freq_hz = 150.0f);
    void stop();

    /** Main-loop state machine; call every iteration. */
    void update();

    bool isActive() const;
    bool isDone() const { return m_state == State::DONE; }
    const char* stateName() const;

    /* Headline results (zero-bias differential inductance). */
    float lastLd() const { return m_ld0; }
    float lastLq() const { return m_lq0; }

    /* Saturation curve results.  biasLdPoint/biasLqPoint/ldPoint/lqPoint
     * return 0 for out-of-range or failed points. */
    int pointCount() const { return m_n_points; }
    float biasLdPoint(int i) const;
    float biasLqPoint(int i) const;
    float ldPoint(int i) const;
    float lqPoint(int i) const;

    /** FOC ISR sample hook; registered with FocControlManager while active. */
    void onFocSample(float id, float iq, float vd, float vq);

    static InductanceCalibrator& instance();

private:
    enum class State {
        IDLE,
        LD_SETTLE,      /* next d bias applied, waiting for it to settle */
        LD_MEASURE,     /* AC on d, accumulating Goertzel */
        LQ_SETTLE,      /* next q bias half (+ or -) applied */
        LQ_MEASURE,     /* AC on q, accumulating Goertzel */
        FINISH,         /* compute results, print summary */
        DONE,
        FAIL
    };

    /* One bias point on the Ld curve, or one half (+ or -) of an Lq point. */
    struct MeasurePoint {
        float bias_a;      /* DC bias for this point [A] */
        bool  axis_q;      /* false: measure d; true: measure q */
        int   lq_index;    /* which Lq curve slot this half feeds (-1 for d) */
        float settle_ms;
    };

    void fail(const char* reason_fmt, ...);
    void enterState(State s);

    bool beginLdPoint(int index);
    bool beginLqHalf(int index, bool negative_half);
    void finishMeasure();
    void resetGoertzel();
    void armSetpoints(const MeasurePoint& mp, bool ac_on);
    float computeInductance(float i_amp, float v_amp) const;

    State m_state = State::IDLE;
    uint32_t m_state_enter_ms = 0;
    char m_fail_reason[96] = {0};

    /* Configuration. */
    float m_max_current_a = 30.0f;
    float m_ac_current_a = 1.5f;
    float m_inj_freq_hz = 150.0f;
    float m_overcurrent_a = 100.0f;  /* abort threshold on measured dq */

    /* Derived at start(): sample counts for the measurement window. */
    uint32_t m_skip_samples = 0;
    uint32_t m_target_samples = 0;
    float m_fs = 2500.0f;          /* control rate [Hz] */
    uint32_t m_settle_n = 0;       /* samples into the current settle */

    /* ISR-to-main-loop abort handoff (fail() must not run in ISR context). */
    volatile bool m_abort_requested = false;
    volatile float m_abort_current_a = 0.0f;

    /* Current-loop gains saved and restored around the run: the calibration
     * needs more loop bandwidth at the injection frequency than the
     * (deliberately conservative) running gains provide. */
    float m_orig_kp = 0.0f;
    float m_orig_ki = 0.0f;
    void restoreGains();

    /* Sequence bookkeeping. */
    int m_point_index = 0;       /* which curve point is being measured */
    bool m_lq_negative_half = false;
    int m_retries_left = 0;      /* amplitude auto-retry for the current point */
    MeasurePoint m_mp = {};

    /* Goertzel accumulator (ISR context). */
    float m_osc_c = 1.0f;        /* oscillator cos/sin at injection freq */
    float m_osc_s = 0.0f;
    float m_osc_dc = 1.0f;       /* per-sample rotation */
    float m_osc_ds = 0.0f;
    uint32_t m_osc_n = 0;
    double m_i_re = 0.0, m_i_im = 0.0;
    double m_v_re = 0.0, m_v_im = 0.0;
    uint32_t m_n = 0;
    float m_peak_abs_i = 0.0f;   /* overcurrent guard */

    /* Results. */
    float m_ld0 = 0.0f;
    float m_lq0 = 0.0f;
    int m_n_points = 0;
    float m_bias_ld[MAX_POINTS] = {};
    float m_bias_lq[MAX_POINTS] = {};
    float m_ld[MAX_POINTS] = {};
    float m_lq[MAX_POINTS] = {};
};

InductanceCalibrator& inductanceCalibrator();

} // namespace Inverter
