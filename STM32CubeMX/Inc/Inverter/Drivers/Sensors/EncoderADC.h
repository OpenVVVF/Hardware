#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Analog sin/cos motor encoder read through ADC2 regular conversions.
 *
 * The encoder sin/cos pins (PC0/PC1) are on ADC2 channels 10 and 11.
 * A dedicated DMA stream (DMA2_Stream0) transfers each completed pair to a
 * circular buffer, and a TIM2 TRGO at 10 kHz triggers the regular sequence.
 *
 * Hard limits from the calibrated commit 0eb9f53 are applied first; dynamic
 * min/max bounds tighten inside those caps as the encoder rotates.  The angle
 * is computed with atan2 and returned in degrees [0, 360).
 */
class EncoderADC {
public:
    EncoderADC() = default;

    /**
     * @brief Initialize ADC2 regular channels, TIM2 trigger, and DMA.
     *
     * Must be called after MX_ADC2_Init() and MX_DMA_Init() have run.
     */
    bool init();

    /**
     * @brief Start regular DMA conversions and the TIM2 trigger.
     */
    bool start();

    /**
     * @brief Read the latest encoder angle.
     *
     * @param[out] angle_deg  Encoder angle in degrees, 0..360.
     * @return true if a new sample was available since the last call.
     */
    bool sample(float& angle_deg);

    /**
     * @brief Atomically read the latest encoder angle and raw sin/cos values.
     *
     * Guarantees that the three returned values came from the same ADC DMA
     * completion, which is necessary for diagnostics and calibration.
     *
     * @param[out] angle_deg  Encoder angle in degrees, 0..360.
     * @param[out] raw_sin    Raw ADC count for the sin channel.
     * @param[out] raw_cos    Raw ADC count for the cos channel.
     * @return true if a new sample was available since the last call.
     */
    bool sample(float& angle_deg, uint16_t& raw_sin, uint16_t& raw_cos);

    /**
     * @brief Latest raw ADC counts and computed angle.
     */
    uint32_t lastRawSin() const { return m_snapshot.raw_sin; }
    uint32_t lastRawCos() const { return m_snapshot.raw_cos; }
    float    lastAngle() const { return m_snapshot.angle; }

    /**
     * @brief Current dynamic amplitude bounds used for normalization.
     */
    uint16_t sinMin() const { return m_sin_min; }
    uint16_t sinMax() const { return m_sin_max; }
    uint16_t cosMin() const { return m_cos_min; }
    uint16_t cosMax() const { return m_cos_max; }

    /**
     * @brief True once both sin and cos have seen enough variation to compute
     * a meaningful angle.
     */
    bool boundsValid() const {
        return (m_sin_max > m_sin_min) && (m_cos_max > m_cos_min);
    }

    /**
     * @brief DMA completion callback, called from DMA2_Stream0_IRQHandler.
     */
    void onDmaComplete();

    /**
     * @brief DMA error callback for the encoder ADC stream.
     */
    void onDmaError();

    /**
     * @brief Main-loop health check for sample timeout.
     *
     * Amplitude-collapse and out-of-range faults are evaluated inside
     * onDmaComplete(); this call only catches a completely stalled DMA stream.
     */
    void diagnose();

    /**
     * @brief Reset dynamic bounds and fault counters.
     *
     * Call before a calibration so the encoder re-learns its amplitude envelope
     * from the current hardware state.  Also restores the hard caps to the
     * factory-calibrated defaults and disables bound learning.
     */
    void resetBounds();

    /**
     * @brief Enable/disable automatic sin/cos min/max learning.
     *
     * When enabled, the fixed hard caps are opened to the full ADC range and the
     * dynamic bounds learn directly from raw samples.  When disabled, the hard
     * caps are set to the learned envelope plus a small margin and normal spike
     * rejection is restored.
     *
     * Call learnBounds(true) at the start of any encoder-recalibration routine
     * and learnBounds(false) when it finishes or fails.
     */
    void learnBounds(bool enable);

private:
    bool configureAdcChannels();
    bool initTimer();
    bool initDma();
    float computeAngle(uint16_t raw_sin, uint16_t raw_cos);

    /* Factory-calibrated hard limits measured in commit 0eb9f53.  These are
     * used as the safe defaults when bound learning is not active. */
    static constexpr uint16_t SIN_MIN_DEFAULT = 427U;
    static constexpr uint16_t SIN_MAX_DEFAULT = 65388U;
    static constexpr uint16_t COS_MIN_DEFAULT = 608U;
    static constexpr uint16_t COS_MAX_DEFAULT = 64743U;

    /* Margin applied around learned bounds when rebuilding the hard caps. */
    static constexpr float    LEARN_MARGIN_FRACTION = 0.05f;
    static constexpr uint16_t LEARN_MARGIN_MIN_COUNTS = 200U;

    /* Active hard caps.  These are opened to full ADC range during learning and
     * tightened back to learned bounds + margin when learning finishes. */
    uint16_t m_sin_min_cap = SIN_MIN_DEFAULT;
    uint16_t m_sin_max_cap = SIN_MAX_DEFAULT;
    uint16_t m_cos_min_cap = COS_MIN_DEFAULT;
    uint16_t m_cos_max_cap = COS_MAX_DEFAULT;

    /* Dynamic bounds start at the opposite hard limits so the first samples
     * tighten them inward to the true signal range. */
    uint16_t m_sin_min = SIN_MAX_DEFAULT;
    uint16_t m_sin_max = SIN_MIN_DEFAULT;
    uint16_t m_cos_min = COS_MAX_DEFAULT;
    uint16_t m_cos_max = COS_MIN_DEFAULT;

    /* True while the hard caps are open and the dynamic bounds learn directly
     * from raw samples. */
    bool m_learning_bounds = false;

    /**
     * @brief Atomic snapshot of one encoder sample.
     *
     * The ISR writes all three fields and then sets m_new_data.  The main loop
     * copies the whole snapshot with interrupts disabled, guaranteeing that
     * angle, raw_sin, and raw_cos are all from the same ADC DMA completion.
     */
    struct Snapshot {
        float    angle = 0.0f;
        uint16_t raw_sin = 0U;
        uint16_t raw_cos = 0U;
    };

    volatile Snapshot m_snapshot;
    volatile bool     m_new_data = false;
    bool              m_running = false;

    /* Fault-detection state. */
    static constexpr uint32_t SAMPLE_TIMEOUT_MS = 5U;
    static constexpr uint16_t MIN_AMP_RANGE     = 20000U;
    static constexpr float    AMP_COLLAPSE_THRESHOLD = 500.0f;
    static constexpr uint16_t AMP_COLLAPSE_COUNT  = 500U;
    static constexpr uint16_t RAIL_MARGIN         = 200U;
    static constexpr uint16_t RAIL_COUNT          = 50U;

    volatile uint32_t m_last_sample_ms = 0;
    volatile uint16_t m_amp_low_count  = 0;
    volatile uint16_t m_rail_count     = 0;
    float             m_mag_ema        = 0.0f;
    bool              m_mag_ema_init   = false;
};

/**
 * @brief Global instance used by the DMA ISR.
 */
EncoderADC& encoderADC();

} // namespace Inverter
