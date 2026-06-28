#pragma once

#include <cstdint>
#include <cstddef>

namespace Inverter {

/**
 * @brief Pair-wise DC resistance calibration for a three-phase motor.
 *
 * Measures the line-to-line resistance of each winding pair by slowly ramping
 * the applied DC voltage and sampling when the measured phase current crosses
 * a configured threshold.  Only the real U and V current sensors are used;
 * the computed W current is never used for a measurement.
 *
 * The individual phase resistances are derived from the three line-to-line
 * measurements assuming a wye-connected stator.
 */
class ResistanceCalibrator {
public:
    ResistanceCalibrator() = default;

    /**
     * @brief Result of the last completed calibration.
     */
    struct Result {
        bool  valid = false;
        bool  imbalance = false;
        float r_uv = 0.0f;
        float r_uw = 0.0f;
        float r_vw = 0.0f;
        float r_u = 0.0f;
        float r_v = 0.0f;
        float r_w = 0.0f;
        float r_avg = 0.0f;
        float imbalance_pct = 0.0f;
        char  message[64] = {};
    };

    /**
     * @brief Configuration for a calibration run.
     */
    struct Config {
        float    max_current_a = 10.0f;  /**< Highest test current [A].          */
        uint32_t num_points = 4;         /**< Number of current setpoints.       */
        uint32_t settle_ms = 100;        /**< Hold time at threshold before sampling. */
        uint32_t sample_window_ms = 100; /**< Averaging window per point [ms].   */
        uint32_t timeout_ms = 15000;     /**< Max time to reach a setpoint [ms]. */
        float    imbalance_tol = 0.25f;  /**< Warn if any phase deviates >25 %.  */
        float    duty_min = 0.0f;        /**< Minimum duty cycle.                */
        float    duty_max = 95.0f;       /**< Max duty to keep ADC sampling window. */
        uint32_t pwm_freq_hz = 0U;       /**< PWM frequency used during cal (0 = keep current). */
    };

    /**
     * @brief Start a new calibration run.
     * @return false if the gate driver is not ready or a motor control run is active.
     */
    bool start();
    bool start(const Config& cfg);

    /**
     * @brief Abort an in-progress calibration and coast the outputs.
     */
    void abort();

    /**
     * @brief State-machine update.  Call from the main loop.
     */
    void service();

    /**
     * @brief True while a calibration is running or cleaning up.
     */
    bool isActive() const;

    /**
     * @brief Human-readable state name for telemetry/shell.
     */
    const char* stateName() const;

    /**
     * @brief Latest completed or in-progress result.
     */
    const Result& lastResult() const { return m_result; }

private:
    enum class State {
        Idle,
        Init,
        CurrentZeroDelay,
        SetupPair,
        Ramp,
        Settle,
        Sample,
        InterPairDelay,
        Compute,
        Done,
        Error
    };

    struct PairData {
        float    v[8] = {};
        float    i[8] = {};
        uint32_t count = 0;
    };

    static constexpr uint32_t MAX_POINTS = 8;

    void enterState(State s);
    void doInit();
    void doCurrentZeroDelay();
    void doSetupPair();
    void doRamp();
    void doSettle();
    void doSample();
    void doInterPairDelay();
    void doCompute();
    void cleanup(bool error);

    float rawMeasuredCurrentForPair() const;
    float filteredMeasuredCurrentForPair();
    void  setPwmForPair();
    void  restorePwm();

    static float linearRegressionSlope(const float v[], const float i[], uint32_t n);

    State    m_state = State::Idle;
    Config   m_cfg;
    Result   m_result;
    PairData m_pair_data[3];

    uint32_t m_pair_index = 0;   /**< 0=UV, 1=UW, 2=VW. */
    uint32_t m_point_index = 0;
    float    m_target_current = 0.0f;

    uint32_t m_state_enter_ms = 0;
    uint32_t m_settle_start_ms = 0;
    uint32_t m_point_start_ms = 0;  /**< When we started trying to reach the current setpoint. */
    uint32_t m_last_update_ms = 0;

    /* Voltage ramp state. */
    float    m_duty = 0.0f;

    /* Current filter state. */
    float    m_i_filt = 0.0f;
    bool     m_i_filt_init = false;

    /* Overcurrent limits used during calibration. */
    float    m_sw_oc_limit = 1000.0f;
    float    m_abort_limit = 100.0f;

    /* Saved overcurrent thresholds to restore after calibration. */
    float    m_saved_sw_oc = 1000.0f;
    float    m_saved_hw_oc = 0.0f;

    /* Sampling accumulators. */
    float    m_i_sum = 0.0f;
    float    m_v_sum = 0.0f;
    uint32_t m_samples = 0;
};

/**
 * @brief Global instance used by the command shell and main loop.
 */
ResistanceCalibrator& resistanceCalibrator();

} // namespace Inverter
