#include "Inverter/Calibration/Common/EncoderTracker.h"

#include "Inverter/Drivers/Sensors/EncoderADC.h"

#include <cmath>

namespace Inverter {

static bool encoderBoundsStable() {
    const auto& enc = encoderADC();
    return enc.boundsValid() &&
           (enc.sinMax() - enc.sinMin() > EncoderTracker::MIN_RANGE) &&
           (enc.cosMax() - enc.cosMin() > EncoderTracker::MIN_RANGE);
}

void EncoderTracker::reset() {
    m_last_angle = encoderADC().lastAngle();
    m_unwrapped_degrees = 0.0f;
    m_start_degrees = 0.0f;
    m_last_move_ms = 0;  /* caller should set after reset if using stalled() */
    m_last_cycles = 0.0f;
    m_wait_for_valid = true;
}

void EncoderTracker::update() {
    if (!encoderBoundsStable()) {
        /* Angle is not meaningful yet.  Only touch the reference while we are
         * still waiting for the first valid sample; once a reference is
         * established, keep it so a later invalid sample does not create a
         * phantom wrap when the next valid sample arrives. */
        if (m_wait_for_valid) {
            m_last_angle = encoderADC().lastAngle();
        }
        return;
    }

    if (m_wait_for_valid) {
        /* First sample with stable bounds becomes the new zero reference. */
        m_last_angle = encoderADC().lastAngle();
        m_wait_for_valid = false;
        return;
    }

    const float angle = encoderADC().lastAngle();
    float delta = angle - m_last_angle;
    if (delta > 180.0f) {
        delta -= 360.0f;
    } else if (delta < -180.0f) {
        delta += 360.0f;
    }
    m_unwrapped_degrees += delta;
    m_last_angle = angle;
}

bool EncoderTracker::stalled(uint32_t now_ms, uint32_t timeout_ms, float threshold_cycles) {
    const float cycles = mechanicalCycles();
    if (std::fabs(cycles - m_last_cycles) > threshold_cycles) {
        m_last_cycles = cycles;
        m_last_move_ms = now_ms;
    }
    return (now_ms - m_last_move_ms) > timeout_ms;
}

} // namespace Inverter
