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
     * @brief Re-run the current-sensor zero-offset calibration safely.
     *
     * Asserts the gate-driver reset, parks PWM at 50 %, and lets the
     * isolated sensor supplies settle before capturing offsets.  Safe to call
     * before each calibration run; returns false if the motor is running or not
     * initialized.
     */
    bool recalibrateOffsets();

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
     * @brief Change modulation index while running (smooth current-limited ramp).
     */
    void setModulationIndex(float modulation_index);

    /**
     * @brief Change modulation index while running without a ramp.
     *
     * Intended for internal calibration loops that update modulation in small
     * steps each iteration.
     */
    void setModulationIndexDirect(float modulation_index);

    /**
     * @brief Safety poll: stop if gate driver faults. Call at ~100 Hz.
     */
    void update();

    bool isRunning() const { return m_running || m_starting; }
    bool isInitialized() const { return m_initialized; }
    float frequencyHz() const { return m_freq_hz; }
    float modulationIndex() const { return m_mod_idx; }

    /**
     * @brief Set the phase-current limit used during modulation ramps [A].
     */
    void setRampCurrentLimit(float amps);

    /**
     * @brief Current phase-current limit used during modulation ramps [A].
     */
    float rampCurrentLimit() const;

private:
    enum class RampState {
        IDLE,
        RAMPING
    };

    enum class StartupState {
        IDLE,
        RESET_ASSERT,
        RESET_RELEASE,
        WAIT_READY,
        STARTED
    };

    void startRamp(float from_m, float to_m, uint32_t ramp_ms,
                   float current_limit_a, bool enable_pole_estimator_on_done);
    void stepRamp(uint32_t now_ms);
    void finishRamp();
    void cancelRamp();
    void stepStartup(uint32_t now_ms);
    void applyModulation(float modulation_index);
    float maxPhaseCurrentMagnitude() const;

    /* Continuous current clamp: throttles the applied modulation whenever the
     * measured phase current exceeds the limit, at ANY bus voltage.  The ramp
     * pause only halts ramp progression; this clamp actually backs the
     * modulation down and recovers slowly. */
    float clampedModulation(float commanded) const;
    void updateCurrentClamp(uint32_t now_ms);

    static constexpr float DEFAULT_RAMP_CURRENT_LIMIT_A = 250.0f;
    static constexpr uint32_t RAMP_PAUSE_TIMEOUT_MS = 200U;

    bool m_initialized = false;
    bool m_running = false;
    bool m_starting = false;
    float m_freq_hz = 0.0f;
    float m_mod_idx = 0.0f;        /* commanded/target modulation index */
    float m_applied_mod_idx = 0.0f; /* modulation actually applied by ramp */
    float m_ramp_current_limit_a = DEFAULT_RAMP_CURRENT_LIMIT_A;

    float m_clamp_mod_ceiling = 1.2f;   /* current-clamp modulation ceiling */
    uint32_t m_clamp_last_ms = 0;

    RampState m_ramp_state = RampState::IDLE;
    float m_ramp_from = 0.0f;
    float m_ramp_to = 0.0f;
    uint32_t m_ramp_start_ms = 0;
    uint32_t m_ramp_duration_ms = 0;
    float m_ramp_active_limit = 0.0f;
    bool m_ramp_paused = false;
    uint32_t m_ramp_pause_start_ms = 0;
    bool m_ramp_enable_pole_estimator = false;

    StartupState m_startup_state = StartupState::IDLE;
    uint32_t m_startup_start_ms = 0;
    uint32_t m_startup_wait_until_ms = 0;
};

/**
 * @brief Global instance used by the command shell and main loop.
 */
OpenLoopController& openLoopController();

} // namespace Inverter
