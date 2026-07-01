#pragma once

#include "Inverter/Calibration/Common/CalibrationHardware.h"

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
        VOLTAGE_STEP, /**< Fixed duty points. */
        CURRENT_CTRL  /**< PI current control. */
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

    static ResistanceCalibrator& instance();

private:
    enum class State {
        IDLE,
        ENABLE,
        WAIT_READY,
        SETTLE,
        MEASURE,
        FINISH_PAIR,
        NEXT_PAIR,
        DONE,
        FAIL
    };

    void enterState(State state);
    void fail(const char* reason_fmt, ...);
    void configureHardware(float duty_pct);
    void restoreHardware();
    void finishPairMeasurement();
    void reportResults();
    void resetMeasurementAccumulators(uint8_t point);

    static const char* pairName(Pair pair);
    static int pairIndex(Pair pair);

    State m_state = State::IDLE;
    CalibrationHardware m_hw;

    Pair   m_pairs[3] = {Pair::UV, Pair::UW, Pair::VW};
    uint8_t m_num_pairs = 3;
    uint8_t m_pair_index = 0;

    Mode     m_mode = Mode::VOLTAGE_STEP;
    static constexpr uint8_t NUM_POINTS = 7U;
    float    m_targets[NUM_POINTS] = {}; /**< duty % (V step) or A (I ctrl). */
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

    float    m_results[3] = {0.0f, 0.0f, 0.0f};
    bool     m_result_valid[3] = {false, false, false};
    float    m_average_r_phase = 0.0f;

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
