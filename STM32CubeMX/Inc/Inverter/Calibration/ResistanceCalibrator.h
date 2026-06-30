#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Phase-to-phase stator resistance calibration.
 *
 * Uses direct TIM1 and GPIO register access to apply a DC voltage across two
 * motor phases while the third phase is placed in true high impedance (both
 * high-side and low-side MOSFETs off).  PWM runs at 8 kHz during calibration
 * for low current ripple.
 *
 * Two measurement modes are supported:
 *  - Voltage step: fixed duty-cycle points; useful for quick checks.
 *  - Current control: a PI loop regulates current to fixed setpoints and the
 *    inverter voltage command (duty * Vdc) is recorded.  This is largely
 *    independent of DC bus voltage.
 *
 * In both modes a linear fit V = I*R_ll + V_offset is performed; the slope
 * R_ll is reported and the offset V_offset (dead-time, switch drops, wiring
 * drops) is discarded.
 *
 * By default the routine runs staged measurements: UV, then UW, then VW.
 * A single pair can be requested instead.
 */
class ResistanceCalibrator {
public:
    ResistanceCalibrator() = default;

    enum class Pair {
        UV,
        UW,
        VW
    };

    enum class Mode {
        VOLTAGE_STEP,  /**< Fixed duty points. */
        CURRENT_CTRL,  /**< PI current control. */
        TOPOLOGY_DIFF  /**< Six-direction + three double-pair differencing. */
    };

    /**
     * @brief Start a voltage-step resistance calibration.
     *
     * @param bus_pct       Maximum percentage of Vdc to apply across the active
     *                      pair.  The routine uses NUM_POINTS evenly spaced
     *                      points from bus_pct/NUM_POINTS up to bus_pct.
     * @param pair          First (or only) pair to measure.
     * @param run_all       If true, measure all three pairs starting from @p pair.
     * @param timeout_ms    Maximum time allowed per pair.
     * @param max_current_a Hard abort threshold for active phase current [A].
     * @return true if calibration started, false if another calibration is
     *         running or the open-loop controller is active.
     */
    bool start(float bus_pct, Pair pair = Pair::UV, bool run_all = true,
               uint32_t timeout_ms = 15000U, float max_current_a = 50.0f);

    /**
     * @brief Start a current-controlled resistance calibration.
     *
     * @param max_current_a Maximum current setpoint [A].  The routine uses
     *                      NUM_POINTS exponentially spaced setpoints from
     *                      CURRENT_CTRL_MIN_A up to max_current_a.
     * @param pair          First (or only) pair to measure.
     * @param run_all       If true, measure all three pairs starting from @p pair.
     * @param timeout_ms    Maximum time allowed per pair.
     * @param oc_limit_a    Hard abort threshold [A].  Must be >= max_current_a.
     * @return true if calibration started, false if another calibration is
     *         running or the open-loop controller is active.
     */
    bool startCurrentCtrl(float max_current_a, Pair pair = Pair::UV,
                          bool run_all = true, uint32_t timeout_ms = 30000U,
                          float oc_limit_a = 0.0f);

    /**
     * @brief Start topology-differenced DC injection calibration.
     *
     * For each current setpoint, measures the six directed single-pair
     * voltage commands and the three split-return double-pair voltage
     * commands.  Fits the high-current slope of ΔV vs I to extract
     * R_total, then builds an empirical inverter-drop LUT.
     *
     * @param max_current_a Maximum current setpoint [A].
     * @param timeout_ms    Maximum time allowed for the whole sweep.
     * @param oc_limit_a    Hard abort threshold [A]. 0 defaults to 1.2·max_current_a.
     * @return true if calibration started, false otherwise.
     */
    bool startTopologyDiff(float max_current_a, uint32_t timeout_ms = 300000U,
                           float oc_limit_a = 0.0f);

    /**
     * @brief Non-blocking state-machine update.  Call at ~100 Hz from the main
     * loop.
     */
    void update();

    /** @brief Abort a running calibration and turn off all switching. */
    void stop();

    /** @brief True while a calibration is running. */
    bool isActive() const {
        return m_state != State::IDLE && m_state != State::DONE &&
               m_state != State::FAIL;
    }

    /** @brief Most recent per-phase resistance for the given pair [ohm]. */
    float lastResult(Pair pair) const;

    /** @brief Average of all successfully measured per-phase resistances [ohm]. */
    float lastAverage() const { return m_average_r_phase; }

    /** @brief Total effective resistance from topology-diff mode [ohm]. */
    float lastRtotal() const { return m_r_total; }

    /** @brief Number of valid points in the inverter-drop LUT. */
    uint8_t lastVceLutCount() const { return m_vce_lut_count; }

    /** @brief Current [A] for inverter-drop LUT entry @p idx. */
    float lastVceLutI(uint8_t idx) const {
        return (idx < m_vce_lut_count) ? m_vce_lut_i[idx] : 0.0f;
    }

    /** @brief Inverter drop [V] for LUT entry @p idx. */
    float lastVceLutV(uint8_t idx) const {
        return (idx < m_vce_lut_count) ? m_vce_lut_v[idx] : 0.0f;
    }

    static ResistanceCalibrator& instance();

private:
    enum class State {
        IDLE,
        ENABLE,
        SETTLE,
        MEASURE,
        FINISH_PAIR,
        NEXT_PAIR,
        DONE,
        FAIL
    };

    /** Six directed phase pairs for the single-pair reference. */
    enum class DirectedPair {
        UV, /**< U high, V low. */
        VU, /**< V high, U low. */
        UW, /**< U high, W low. */
        WU, /**< W high, U low. */
        VW, /**< V high, W low. */
        WV  /**< W high, V low. */
    };

    /** Three split-return double-pair topologies. */
    enum class DoublePair {
        U_VW, /**< U high, V and W low. */
        V_UW, /**< V high, U and W low. */
        W_UV  /**< W high, U and V low. */
    };

    void enterState(State state);
    void fail(const char* reason);
    bool enableGateDriver();
    void configureHardware(float duty_pct);
    void configureHardware(DirectedPair pair, float duty_pct);
    void configureHardware(DoublePair pair, float duty_pct);
    void configureHardwareImpl(const bool is_high[3], const bool is_low[3], float duty_pct);
    void restoreHardware();
    void finishPairMeasurement();
    void finishTopologyDiff();
    void reportResults();
    void reportTopologyDiffResults();
    void resetMeasurementAccumulators(uint8_t point);
    void resetSubstepAccumulators();
    void advanceTopologyDiffSubstep();
    float activeCurrentForDirectedPair(float iu, float iv, float iw, DirectedPair dp) const;
    float inactiveCurrentForDirectedPair(float iu, float iv, float iw, DirectedPair dp) const;
    bool symmetryOkForDoublePair(float iu, float iv, float iw, DoublePair dp, float i_set) const;

    static const char* pairName(Pair pair);
    static int pairIndex(Pair pair);
    static const char* directedPairName(DirectedPair dp);
    static const char* doublePairName(DoublePair dp);

    State m_state = State::IDLE;

    Pair   m_pairs[3] = {Pair::UV, Pair::UW, Pair::VW};
    uint8_t m_num_pairs = 3;
    uint8_t m_pair_index = 0;

    Mode     m_mode = Mode::VOLTAGE_STEP;
    static constexpr uint8_t NUM_POINTS = 7U;
    static constexpr uint8_t TOPOLOGY_DIFF_POINTS = 15U;
    float    m_targets[NUM_POINTS] = {}; /**< duty % (V step) or A (I ctrl). */
    float    m_td_targets[TOPOLOGY_DIFF_POINTS] = {}; /**< current setpoints [A]. */
    static constexpr float CURRENT_CTRL_MIN_A = 4.0f; /**< lowest current setpoint. */
    float    m_max_current_a = 50.0f;
    uint32_t m_timeout_ms = 5000U;

    uint8_t  m_point_index = 0;
    uint32_t m_state_enter_ms = 0;

    /* Measurement accumulators.  Index = measurement point. */
    uint32_t m_sample_count[NUM_POINTS] = {};
    float    m_sum_i_active[NUM_POINTS] = {};
    float    m_sum_i_inactive[NUM_POINTS] = {};
    float    m_sum_vdc[NUM_POINTS] = {};
    float    m_sum_duty[NUM_POINTS] = {}; /**< commanded duty %. */

    /* Topology-diff state. */
    uint8_t  m_topology_point_index = 0;
    uint8_t  m_substep_index = 0; /**< 0-5 single-pair, 6-8 double-pair. */
    float    m_td_v1_sum = 0.0f;
    float    m_td_v2_sum = 0.0f;
    uint32_t m_td_v1_count = 0;
    uint32_t m_td_v2_count = 0;

    /* Substep accumulators (reset each substep). */
    uint32_t m_sub_sample_count = 0;
    float    m_sub_vdc_sum = 0.0f;
    float    m_sub_duty_sum = 0.0f;
    float    m_sub_i_active_sum = 0.0f;
    float    m_sub_i_ret1_sum = 0.0f;
    float    m_sub_i_ret2_sum = 0.0f;

    /* Topology-diff per-point results. */
    float    m_td_i[TOPOLOGY_DIFF_POINTS] = {};
    float    m_td_v1[TOPOLOGY_DIFF_POINTS] = {};
    float    m_td_v2[TOPOLOGY_DIFF_POINTS] = {};
    bool     m_td_valid[TOPOLOGY_DIFF_POINTS] = {};

    /* Extracted results. */
    float    m_results[3] = {0.0f, 0.0f, 0.0f};
    bool     m_result_valid[3] = {false, false, false};
    float    m_average_r_phase = 0.0f;
    float    m_r_total = 0.0f;
    float    m_vce_lut_i[TOPOLOGY_DIFF_POINTS] = {};
    float    m_vce_lut_v[TOPOLOGY_DIFF_POINTS] = {};
    uint8_t  m_vce_lut_count = 0;

    /* PI current-control state. */
    float    m_pi_integral = 0.0f;
    float    m_pi_duty = 0.0f;
    uint32_t m_pi_last_ms = 0;

    /* Timing instrumentation (diagnostic only). */
    uint32_t m_update_calls = 0;      /**< update() calls in current log window. */
    uint32_t m_sample_calls = 0;      /**< successful ADC samples in current window. */
    uint32_t m_last_rate_log_ms = 0;  /**< last time rates were logged. */
    uint32_t m_last_sample_ms = 0;    /**< last time a new ADC sample was seen. */

    /* Saved hardware state for restore. */
    uint32_t m_saved_arr = 0;
    uint32_t m_saved_psc = 0;
    uint32_t m_saved_ccer = 0;
    uint32_t m_saved_ccr1 = 0;
    uint32_t m_saved_ccr2 = 0;
    uint32_t m_saved_ccr3 = 0;
    uint32_t m_saved_bdtr = 0;
    uint32_t m_saved_gpioe_moder = 0;

    static constexpr float MAX_BUS_PCT = 25.0f;
    static constexpr uint32_t CAL_ARR = 17186U; /**< ~8 kHz center-aligned with 275 MHz timer clock. */
    static constexpr uint32_t SETTLE_TIME_MS = 1000U;
    static constexpr uint32_t MEASURE_TIME_MS = 1000U;
    static constexpr uint32_t MIN_SAMPLES = 2000U;
    static constexpr float MAX_INACTIVE_CURRENT_RATIO = 0.05f; /**< 5 % of active current. */
    static constexpr float MAX_INACTIVE_CURRENT_MIN_A = 2.00f;  /**< floor for the ratio check. */

    static constexpr float PI_KP = 0.05f; /**< % duty per A error. */
    static constexpr float PI_KI = 10.0f; /**< % duty per A per second. */
    static constexpr float PI_MIN_DUTY = 0.05f; /**< 0.05 %, avoids zero-crossing issues. */
};

ResistanceCalibrator& resistanceCalibrator();

} // namespace Inverter
