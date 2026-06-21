#pragma once

#include <cstdint>
#include <cstddef>

namespace Inverter {

/**
 * @brief PWM-synchronous phase-current ADC for FOC.
 *
 * Uses ADC1 and ADC2 in independent scan conversion, both triggered by the same
 * TIM1 TRGO edge and transferred by separate DMA streams.  Each ADC result is
 * read from its own DR, avoiding the STM32H7 dual-mode common-data-register
 * packing mis-alignment that can swap/delay ADC2 samples.
 *
 * ADC1 sequence: U signal (CH4) -> V signal (CH3)
 * ADC2 sequence: U reference (CH8) -> V reference (CH7)
 *
 * Ranks are paired across the two ADCs so each TIM1 trigger samples both U and V
 * differentially: signal and reference are captured together.  Index 0 is the U
 * pair, index 1 is the V pair.  W current is computed as -(iu + iv).
 */
class PhaseCurrentADC {
public:
    PhaseCurrentADC() = default;

    /**
     * @brief Initialize hardware: ADC channels, TIM1 TRGO, DMA.
     *
     * Must be called after MX_ADC1_Init(), MX_ADC2_Init(), MX_TIM1_Init()
     * and MX_DMA_Init() have run.
     */
    bool init();

    /**
     * @brief Start timer-triggered DMA conversions and run a one-shot zero-current
     * offset calibration.  The motor must be at standstill with no phase current.
     */
    bool start();

    /**
     * @brief Stop conversions.
     */
    bool stop();

    /**
     * @brief Convert the latest raw samples to amperes.
     *
     * @param[out] iu  Phase U current in A.
     * @param[out] iv  Phase V current in A.
     * @param[out] iw  Phase W current in A (computed).
     * @return true if a new sample pair was available since the last call.
     */
    bool sample(float& iu, float& iv, float& iw);

    /**
     * @brief Raw ADC callbacks, called from the DMA ISR.
     */
    void onDmaHalfComplete();
    void onDmaComplete();

    /**
     * @brief Diagnostic read-back of the latest raw ADC counts.
     */
    uint32_t lastRawUSig() const { return m_raw_u_sig; }
    uint32_t lastRawVSig() const { return m_raw_v_sig; }
    uint32_t lastRawURef() const { return m_raw_u_ref; }
    uint32_t lastRawVRef() const { return m_raw_v_ref; }
    float    lastOffsetU() const { return m_offset_u; }
    float    lastOffsetV() const { return m_offset_v; }

private:
    bool configureAdcChannels();
    bool initDma();
    bool initTrigger();
    bool calibrateOffsets();
    void updateFilter(float raw_u, float raw_v);
    float countsToCurrent(uint32_t sig, uint32_t ref) const;

    static constexpr uint32_t ADC_BITS        = 16;
    static constexpr float    ADC_VREF        = 3.3f;
    static constexpr float    DIVIDER         = 2.0f / 3.0f;
    static constexpr float    SENSITIVITY_VA  = 1.042e-3f; /**< LA37S600. */

    /* Lightweight moving-average filter applied at the ADC sample rate to
     * reduce sensor/ADC noise before the slower telemetry/FOC loop reads it.
     * A length of 8 gives ~sqrt(8) noise reduction with ~0.8 ms latency. */
    static constexpr size_t FILTER_LEN = 8;

    volatile uint32_t m_raw_u_sig = 0;
    volatile uint32_t m_raw_v_sig = 0;
    volatile uint32_t m_raw_u_ref = 0;
    volatile uint32_t m_raw_v_ref = 0;
    volatile float    m_iu = 0.0f;
    volatile float    m_iv = 0.0f;
    float             m_offset_u = 0.0f;
    float             m_offset_v = 0.0f;

    float             m_filter_buf_u[FILTER_LEN] = {};
    float             m_filter_buf_v[FILTER_LEN] = {};
    float             m_filter_sum_u = 0.0f;
    float             m_filter_sum_v = 0.0f;
    float             m_filtered_u = 0.0f;
    float             m_filtered_v = 0.0f;
    size_t            m_filter_idx = 0;
    size_t            m_filter_count = 0;

    volatile bool     m_new_data = false;
    bool              m_running = false;
};

/**
 * @brief Global instance used by the DMA ISR.
 */
PhaseCurrentADC& phaseCurrentADC();

} // namespace Inverter
