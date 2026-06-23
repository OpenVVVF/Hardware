#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Safe open-loop PMSM control.
 *
 * Initializes the gate driver and TIM1 PWM outputs, then starts/stops the
 * SVPWM angle ramp.  Frequency and modulation index can be changed at runtime.
 */
class OpenLoopController {
public:
    OpenLoopController() = default;

    /**
     * @brief Initialize gate driver, PWM frequency/deadtime, and enable outputs.
     *
     * Does NOT start the rotating field; call start() for that.
     */
    bool init();

    /**
     * @brief Start the SVPWM ramp.
     *
     * @param freq_hz          Electrical fundamental frequency in Hz.
     * @param modulation_index 0..1.15 (SVPWM linear limit).
     * @return true if gate driver is ready and no fault is present.
     */
    bool start(float freq_hz, float modulation_index);

    /**
     * @brief Stop the SVPWM ramp and park outputs.
     */
    void stop();

    /**
     * @brief Change electrical frequency while running.
     */
    void setFrequency(float freq_hz);

    /**
     * @brief Change modulation index while running.
     */
    void setModulationIndex(float modulation_index);

    /**
     * @brief Safety poll: stop if gate driver faults. Call at ~100 Hz.
     */
    void update();

    bool isRunning() const { return m_running; }
    float frequencyHz() const { return m_freq_hz; }
    float modulationIndex() const { return m_mod_idx; }

private:
    void rampModulation(float from_m, float to_m, uint32_t ramp_ms);

    bool m_initialized = false;
    bool m_running = false;
    float m_freq_hz = 0.0f;
    float m_mod_idx = 0.0f;
};

/**
 * @brief Global instance used by the command shell and main loop.
 */
OpenLoopController& openLoopController();

} // namespace Inverter
