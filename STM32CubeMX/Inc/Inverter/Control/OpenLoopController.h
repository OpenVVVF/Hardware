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

    /**
     * @brief Run an automated pole-pair calibration.
     *
     * Starts at 1 Hz, ramps modulation until the encoder reliably turns, then
     * counts commanded electrical cycles vs encoder mechanical cycles and
     * reports the ratio.  Non-blocking; progress is handled in update().
     */
    bool startCalibration();

    bool isRunning() const { return m_running; }
    bool isCalibrating() const { return m_cal_state != CalState::IDLE; }
    float frequencyHz() const { return m_freq_hz; }
    float modulationIndex() const { return m_mod_idx; }

private:
    void rampModulation(float from_m, float to_m, uint32_t ramp_ms);
    void applyModulation(float modulation_index);
    void runCalibration();
    void reportCalibrationRatio(const char* label);

    enum class CalState {
        IDLE,
        RAMP,
        COUNT,
        DONE,
        FAIL
    };

    bool m_initialized = false;
    bool m_running = false;
    float m_freq_hz = 0.0f;
    float m_mod_idx = 0.0f;

    /* Calibration state. */
    CalState m_cal_state = CalState::IDLE;
    float m_cal_mod = 0.0f;
    float m_cal_mech_start = 0.0f;
    float m_cal_mech_count_start = 0.0f;
    uint32_t m_cal_elec_count_start = 0;
    uint32_t m_cal_last_ramp_ms = 0;
    uint32_t m_cal_count_start_ms = 0;
    uint32_t m_cal_last_move_ms = 0;
    float m_cal_last_mech = 0.0f;
};

/**
 * @brief Global instance used by the command shell and main loop.
 */
OpenLoopController& openLoopController();

} // namespace Inverter
