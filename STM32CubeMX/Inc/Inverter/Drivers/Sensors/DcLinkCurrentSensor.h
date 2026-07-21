#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief DC-link current sensor (LA37S600 on ADC1_INP2 / ADC1_INP6).
 *
 * The DC-link current uses the same differential signal + reference scheme
 * as the phase-current sensors, read on ADC1 channels 2 (signal) and 6
 * (reference) with polled regular conversions from the main loop.  Power
 * monitoring does not need PWM-synchronized sampling; ~40 Hz is plenty and
 * keeps the control-critical injected path untouched.
 *
 * Also integrates input power into cumulative energy [Wh].
 */
class DcLinkCurrentSensor {
public:
    bool init();

    /** Main-loop poll: performs one conversion pair every POLL_MS. */
    void update();

    /** Re-capture the zero-current offset and reset the energy counter.
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
    static constexpr uint32_t POLL_MS = 25U;

    float countsToCurrent(uint32_t sig, uint32_t ref) const;
    bool configChannel(uint32_t channel);
    bool readPair();

    uint32_t m_last_poll_ms = 0;
    uint32_t m_last_energy_ms = 0;

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
