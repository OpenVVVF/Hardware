#pragma once

#include <cstdint>
#include <cstddef>

namespace Inverter {

/**
 * @brief Estimate motor poles from current zero crossings and raw sin/cos.
 *
 * Uses a sliding window over the last N mechanical cycles:
 *
 *     poles = 2 * electrical_zero_crossings_in_window / mechanical_cycles_in_window
 *
 * This is much more robust than a time-based frequency ratio because it is
 * independent of encoder calibration, DC offset drift, and speed ripple.
 *
 * IMPORTANT: the estimate is only the true pole count if the encoder
 * sin/cos sensor produces exactly one cycle per mechanical revolution.  If
 * the encoder magnet has N cycles/rev, the reported value is poles / N.
 */
class PoleEstimator {
public:
    PoleEstimator() = default;

    /**
     * @brief Set the commanded electrical frequency (Hz).
     *
     * Only used as a temporary hint before enough mechanical cycles have been
     * accumulated.
     */
    void setElectricalFrequency(float f_hz);

    /**
     * @brief Enable/disable estimation.  Enabling resets accumulators and
     * captures the current raw sin/cos values as DC offsets.
     */
    void setEnabled(bool enabled, uint16_t raw_sin = 0, uint16_t raw_cos = 0);

    /**
     * @brief Reset all accumulators and the filtered estimate.
     */
    void reset();

    /**
     * @brief Process one current/encoder sample pair.
     *
     * Intended to be called from the current-sense ISR at the ADC sample rate.
     */
    void onSample(float iu, uint16_t raw_sin, uint16_t raw_cos);

    /**
     * @brief Current filtered pole estimate.  0 if not enough data yet.
     */
    float estimate() const { return m_filtered_poles; }

    /**
     * @brief Accumulated mechanical and electrical cycle counts.
     */
    float mechanicalCycles() const { return m_mech_cycles; }
    float electricalCycles() const { return m_elec_cycles; }

    /**
     * @brief Latest windowed raw estimate before low-pass filtering.
     */
    float windowEstimate() const { return m_window_poles; }

    /**
     * @brief Manual encoder cycle counting for sensor calibration.
     *
     * While active, positive zero crossings of the filtered sin/cos signal are
     * counted independently of the pole estimate.  This lets you rotate
     * the shaft by hand exactly one revolution and read off how many sin/cos
     * periods the encoder produces per mechanical revolution.
     */
    void startManualEncoderCal();
    void stopManualEncoderCal();
    float manualEncoderCycles() const { return m_manual_mech_cycles; }
    bool isManualEncoderCalActive() const { return m_manual_cal; }

    /**
     * @brief Global instance.
     */
    static PoleEstimator& instance();

private:
    static constexpr size_t WINDOW_CYCLES = 5;

    bool    m_enabled = false;
    float   m_f_elec_hz = 0.0f;

    /* DC offsets for raw sin/cos. */
    float   m_sin_offset = 0.0f;
    float   m_cos_offset = 0.0f;

    /* Low-pass filtered sin/cos and current used for robust crossing detection. */
    float   m_sin_filt = 0.0f;
    float   m_cos_filt = 0.0f;
    float   m_iu_filt = 0.0f;

    /* Mechanical cycle detection. */
    int     m_mech_state = 0;
    int     m_prev_mech_state = 0;
    float   m_mech_cycles = 0.0f;

    /* Manual encoder cycle calibration. */
    bool    m_manual_cal = false;
    bool    m_manual_prev_enabled = false;
    float   m_manual_mech_cycles = 0.0f;

    /* Electrical cycle detection. */
    int     m_current_state = 0;
    int     m_prev_current_state = 0;
    float   m_iu_peak = 0.0f;
    float   m_elec_cycles = 0.0f;

    /* Sliding window: electrical cycle count at each completed mech cycle. */
    float   m_elec_at_mech[WINDOW_CYCLES] = {};
    size_t  m_mech_window_idx = 0;

    float   m_window_poles = 0.0f;
    float   m_filtered_poles = 0.0f;
};

} // namespace Inverter
