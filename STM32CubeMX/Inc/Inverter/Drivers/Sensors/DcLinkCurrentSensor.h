#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief DC-link current sensor (LA37S600 on ADC1_INP2 / ADC1_INP6).
 *
 * The DC-link current uses the same differential signal + reference scheme
 * as the phase-current sensors and is sampled in the SAME PWM-synchronized
 * injected sequence (ADC1 injected ranks 3/4) as the phase currents.  That
 * keeps signal and reference microseconds apart, so the sensor supply bounce
 * is common-mode and cancels in the difference - polled regular conversions
 * proved unusable here (kHz supply ripple aliased into the sequential reads).
 *
 * Also integrates input power into cumulative energy [Wh].
 */
class DcLinkCurrentSensor {
public:
    bool init();

    /** Main-loop poll: consumes the latest injected sample, finishes the
     *  zero-offset capture window, then publishes telemetry. */
    void update();

    /** Restart the zero-offset capture and reset the energy counter.
     *  Only meaningful with the drive idle (no switching). */
    bool zeroCalibrate();

    float current() const { return m_current_a; }
    float power() const { return m_power_w; }
    float energyWh() const { return m_energy_wh; }
    bool offsetValid() const { return m_offset_valid; }

    uint32_t lastRawSig() const { return m_raw_sig; }
    uint32_t lastRawRef() const { return m_raw_ref; }
    float    lastOffset() const { return m_offset_a; }

    static DcLinkCurrentSensor& instance();

private:
    float countsToCurrent(uint32_t sig, uint32_t ref) const;

    uint32_t m_last_energy_ms = 0;
    uint32_t m_zero_samples_left = 0;
    double   m_zero_acc = 0.0;

    uint32_t m_raw_sig = 0;
    uint32_t m_raw_ref = 0;
    float m_offset_a = 0.0f;
    bool  m_offset_valid = false;

    float m_current_a = 0.0f;
    float m_power_w = 0.0f;
    float m_energy_wh = 0.0f;
};

DcLinkCurrentSensor& dcLinkCurrentSensor();

} // namespace Inverter
