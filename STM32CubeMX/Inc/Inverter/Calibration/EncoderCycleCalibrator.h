#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Manual encoder-cycle counter for sensor calibration.
 *
 * While active, positive zero crossings of the low-pass-filtered raw sin/cos
 * signal are counted.  Rotating the shaft exactly one mechanical revolution
 * and reading the cycle count tells you how many sin/cos periods the encoder
 * produces per revolution.
 *
 * Intended to be called from the current-sense ISR at the ADC sample rate.
 */
class EncoderCycleCalibrator {
public:
    EncoderCycleCalibrator() = default;

    /** @brief Start counting cycles. Resets filters and the cycle counter. */
    void start();

    /** @brief Stop counting cycles. */
    void stop();

    /** @brief Reset filters and cycle counter without changing active state. */
    void reset();

    /** @brief Process one raw encoder sample pair. */
    void onSample(uint16_t raw_sin, uint16_t raw_cos);

    /** @brief Current accumulated mechanical cycles (positive zero crossings). */
    float cycles() const { return m_cycles; }

    /** @brief True if the calibrator is currently counting. */
    bool isActive() const { return m_active; }

    /** @brief Global instance. */
    static EncoderCycleCalibrator& instance();

private:
    bool  m_active = false;

    float m_sin_offset = 0.0f;
    float m_cos_offset = 0.0f;

    float m_sin_filt = 0.0f;
    float m_cos_filt = 0.0f;

    int   m_state = 0;
    int   m_prev_state = 0;
    float m_cycles = 0.0f;
};

} // namespace Inverter
