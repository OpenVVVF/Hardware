#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Phase-to-phase stator resistance calibration.
 *
 * Applies a fixed percentage of the DC bus across two motor phases while the
 * third phase is driven to the neutral (50 %) point.  Two voltage points are
 * measured per pair; the resistance is computed from the slope dV/dI, which
 * cancels inverter dead-time and MOSFET-drop offsets.
 *
 * By default the routine runs three staged measurements: UV, then UW, then VW.
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

    /**
     * @brief Start a resistance calibration.
     *
     * @param bus_pct       Percentage of Vdc to apply line-to-line at the high
     *                      point.  The low point is half of this value.
     * @param pair          First (or only) pair to measure.
     * @param run_all       If true, measure all three pairs starting from @p pair.
     *                      If false, measure only @p pair.
     * @param timeout_ms    Maximum time allowed per pair.
     * @param max_current_a Hard abort threshold for active phase current [A].
     * @return true if calibration started, false if another calibration is
     *         running or the open-loop controller is active.
     */
    bool start(float bus_pct, Pair pair = Pair::UV, bool run_all = true,
               uint32_t timeout_ms = 5000U, float max_current_a = 50.0f);

    /**
     * @brief Non-blocking state-machine update.  Call at ~100 Hz from the main
     * loop.
     */
    void update();

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
        SETTLE_A,
        MEASURE_A,
        SETTLE_B,
        MEASURE_B,
        NEXT_PAIR,
        DONE,
        FAIL
    };

    void enterState(State state);
    void configurePair(Pair pair, float bus_pct);
    void finishPairMeasurement();
    void reportResults();
    void cleanup();

    static const char* pairName(Pair pair);
    static int pairIndex(Pair pair);

    State m_state = State::IDLE;

    Pair   m_pairs[3] = {Pair::UV, Pair::UW, Pair::VW};
    uint8_t m_num_pairs = 3;
    uint8_t m_pair_index = 0;

    float  m_bus_pct_a = 0.0f; /**< High-point bus percentage. */
    float  m_bus_pct_b = 0.0f; /**< Low-point bus percentage (half of A). */
    float  m_max_current_a = 50.0f;
    uint32_t m_timeout_ms = 5000U;
    uint32_t m_original_freq_hz = 10000U;

    uint32_t m_state_enter_ms = 0;

    /* Measurement accumulators.  Index 0 = high point, 1 = low point. */
    uint32_t m_sample_count[2] = {0, 0};
    float    m_sum_i_active[2] = {0.0f, 0.0f};
    float    m_sum_i_inactive[2] = {0.0f, 0.0f};
    float    m_sum_vdc[2] = {0.0f, 0.0f};

    float    m_results[3] = {0.0f, 0.0f, 0.0f};
    bool     m_result_valid[3] = {false, false, false};
    float    m_average_r_phase = 0.0f;

    static constexpr float MAX_BUS_PCT = 25.0f;
    static constexpr uint32_t CAL_SWITCHING_FREQ_HZ = 5000U;
    static constexpr uint32_t SETTLE_TIME_MS = 200U;
    static constexpr uint32_t MEASURE_TIME_MS = 200U;
    static constexpr uint32_t MIN_SAMPLES = 500U;
    static constexpr float MAX_INACTIVE_CURRENT_RATIO = 0.05f; /**< 5 % of active current. */
    static constexpr float MAX_INACTIVE_CURRENT_MIN_A = 0.50f;  /**< floor for the ratio check. */
};

ResistanceCalibrator& resistanceCalibrator();

} // namespace Inverter
