#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Estimate motor pole pairs from current zero crossings and encoder angle.
 *
 * Assumes the encoder produces one 0..360 degree cycle per mechanical
 * revolution.  The phase-U current has one positive zero crossing per
 * electrical cycle, so over many revolutions:
 *
 *     pole_pairs = electrical_cycles / mechanical_revolutions
 *
 * The estimate is low-pass filtered and refined continuously while the motor
 * is running.
 */
class PolePairEstimator {
public:
    PolePairEstimator() = default;

    /**
     * @brief Enable/disable estimation.  Enabling resets accumulators.
     */
    void setEnabled(bool enabled);

    /**
     * @brief Reset all accumulators and the filtered estimate.
     */
    void reset();

    /**
     * @brief Process one current/encoder sample pair.
     *
     * Intended to be called from the current-sense ISR at the ADC sample rate.
     *
     * @param iu            Phase U current after offset subtraction (A).
     * @param enc_angle_deg Encoder angle in degrees, 0..360.
     */
    void onSample(float iu, float enc_angle_deg);

    /**
     * @brief Current filtered pole-pair estimate.  0 if not enough data yet.
     */
    float estimate() const { return m_filtered_pp; }

    /**
     * @brief Accumulated mechanical revolutions and electrical cycles.
     */
    float revolutions() const { return m_mech_deg_total / 360.0f; }
    float electricalCycles() const { return m_elec_cycles; }

    /**
     * @brief Global instance.
     */
    static PolePairEstimator& instance();

private:
    bool   m_enabled = false;
    float  m_last_angle_deg = 0.0f;
    float  m_mech_deg_total = 0.0f;
    float  m_elec_cycles = 0.0f;

    int    m_current_state = 0;   /**< -1 = below -threshold, +1 = above +threshold. */
    int    m_prev_state = 0;

    float  m_raw_pp = 0.0f;
    float  m_filtered_pp = 0.0f;
};

} // namespace Inverter
