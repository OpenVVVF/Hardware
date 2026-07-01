#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Unwraps an absolute encoder angle and reports moved distance / stall.
 *
 * Tracks mechanical degrees and cycles without relying on calibrated encoder
 * bounds.  Useful for any calibration that needs to know how far the rotor
 * has turned.
 *
 * The tracker waits until the encoder dynamic bounds have enough span before
 * accepting samples.  This prevents the large fake deltas that occur while the
 * angle is invalid immediately after resetBounds() or boot.
 */
class EncoderTracker {
public:
    EncoderTracker() = default;

    /** Reset tracking; current encoder angle becomes the zero reference. */
    void reset();

    /** Update from encoderADC().lastAngle(). */
    void update();

    float unwrappedDegrees() const { return m_unwrapped_degrees; }
    float mechanicalCycles() const { return m_unwrapped_degrees / 360.0f; }

    /** Degrees moved since the last reset(). */
    float movedDegrees() const { return m_unwrapped_degrees - m_start_degrees; }

    /** True if no significant movement has occurred for timeoutMs. */
    bool stalled(uint32_t now_ms, uint32_t timeout_ms, float threshold_cycles = 0.01f);

    static constexpr uint16_t MIN_RANGE = 10000U;

private:

    float m_last_angle = 0.0f;
    float m_unwrapped_degrees = 0.0f;
    float m_start_degrees = 0.0f;
    uint32_t m_last_move_ms = 0;
    float m_last_cycles = 0.0f;
    bool  m_wait_for_valid = true;
};

} // namespace Inverter
