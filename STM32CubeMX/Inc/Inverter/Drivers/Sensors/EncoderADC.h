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
     * @brief Latest raw ADC counts and computed angle.
     */
    uint32_t lastRawSin() const { return m_raw_sin; }
    uint32_t lastRawCos() const { return m_raw_cos; }
    float    lastAngle() const { return m_angle; }

    /**
     * @brief DMA completion callback, called from DMA2_Stream0_IRQHandler.
     */
    void onDmaComplete();

private:
    bool configureAdcChannels();
    bool initTimer();
    bool initDma();
    float computeAngle(uint16_t raw_sin, uint16_t raw_cos);

    /* Hard limits measured/calibrated in commit 0eb9f53. */
    static constexpr uint16_t SIN_MIN_CAP = 427U;
    static constexpr uint16_t SIN_MAX_CAP = 65388U;
    static constexpr uint16_t COS_MIN_CAP = 608U;
    static constexpr uint16_t COS_MAX_CAP = 64743U;

    /* Dynamic bounds start at the hard limits and tighten inward. */
    uint16_t m_sin_min = SIN_MIN_CAP;
    uint16_t m_sin_max = SIN_MAX_CAP;
    uint16_t m_cos_min = COS_MIN_CAP;
    uint16_t m_cos_max = COS_MAX_CAP;

    volatile uint16_t m_raw_sin = 0;
    volatile uint16_t m_raw_cos = 0;
    volatile float    m_angle = 0.0f;
    volatile bool     m_new_data = false;
    bool              m_running = false;
};

/**
 * @brief Global instance used by the DMA ISR.
 */
EncoderADC& encoderADC();

} // namespace Inverter
