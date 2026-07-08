#pragma once

#include "Inverter/Control/FocController.h"

#include <cstdint>

namespace Inverter {

/**
 * @brief Top-level manager for closed-loop FOC operation.
 *
 * Handles gate-driver sequencing, safe start/stop, setpoint limits, and
 * telemetry.  The actual current loop is executed from the TIM1 update ISR
 * via onPwmPeriod().
 */
class FocControlManager {
public:
    FocControlManager() = default;

    /**
     * @brief One-time initialization. Does not enable the power stage.
     */
    bool init();

    /**
     * @brief Start the FOC controller and enable the gate driver.
     *
     * @param iq_a  Initial torque current [A].
     * @param id_a  Initial flux current [A], default 0.
     * @return true if the startup sequence began successfully.
     */
    bool start(float iq_a, float id_a = 0.0f);

    /**
     * @brief Stop the FOC controller and disable the power stage.
     */
    void stop();

    void setId(float id_a);
    void setIq(float iq_a);
    void setKp(float kp);
    void setKi(float ki);
    void setVoltageLimit(float v_v); ///< 0 = auto from DC bus.

    /**
     * @brief Add a delta to the encoder mechanical offset [deg].
     *
     * Useful for tuning the offset until the motor spins.  The delta is added
     * to the calibration-derived offset.  Call with the motor stopped or at
     * very low current.
     */
    void adjustEncoderOffset(float delta_mech_deg);

    /**
     * @brief Set the encoder direction sign (+1 or -1).
     *
     * The calibrator detects whether the encoder increases or decreases as the
     * stator field rotates.  If the rotor locks instead of spinning, try the
     * opposite sign.
     */
    void setEncoderSign(float sign);

    /**
     * @brief Current total encoder offset used by FOC [mech deg].
     */
    float encoderOffsetDeg() const;

    bool isRunning() const { return m_running || m_starting; }
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Current setpoints after limiting.
     */
    const FocSetpoints& setpoints() const { return m_setpoints; }

    /**
     * @brief Access the underlying FOC controller (for telemetry/ISR).
     */
    FocController& controller() { return m_controller; }
    const FocController& controller() const { return m_controller; }

    /**
     * @brief Most recent phase currents consumed by the FOC ISR [A].
     */
    float lastIuA() const { return m_last_iu_a; }
    float lastIvA() const { return m_last_iv_a; }
    float lastIwA() const { return m_last_iw_a; }
    uint32_t missedCurrentSamples() const { return m_missed_current_samples; }

    /**
     * @brief Main-loop safety poll. Call at ~100 Hz.
     */
    void update();

    /**
     * @brief Execute one FOC step from the TIM1 update ISR.
     */
    void onPwmPeriod();

private:
    enum class StartupState {
        IDLE,
        RESET_ASSERT,
        RESET_RELEASE,
        WAIT_READY,
        STARTED
    };

    void stepStartup(uint32_t now_ms);
    void applySetpointLimits();
    void logTelemetry();
    bool isAnyCalibrationActive() const;
    bool checkSensorReadiness();
    void requestSafeStopFromIsr();

    static constexpr uint32_t STARTUP_TIMEOUT_MS = 500U;
    static constexpr uint32_t RESET_ASSERT_MS = 10U;
    static constexpr uint32_t RESET_RELEASE_MS = 10U;
    static constexpr float DEFAULT_KP = 0.03f;
    static constexpr float DEFAULT_KI = 10.0f;
    static constexpr float DEFAULT_SOFT_VOLTAGE_LIMIT_V = 0.0f;
    static constexpr float MIN_VDC_V = 5.0f;            /**< Minimum valid DC-bus voltage. */
    static constexpr uint32_t MAX_MISSED_CURRENT_SAMPLES = 5U;

    FocController m_controller;
    FocConfig m_config;
    FocSetpoints m_setpoints;
    MotorParameters m_motor;
    float m_encoder_offset_adjustment_deg = 0.0f;

    bool m_initialized = false;
    volatile bool m_running = false;
    volatile bool m_starting = false;
    volatile bool m_stop_requested_from_isr = false;

    float m_dt_s = 0.0f;

    StartupState m_startup_state = StartupState::IDLE;
    uint32_t m_startup_start_ms = 0;
    uint32_t m_startup_wait_until_ms = 0;

    uint32_t m_missed_current_samples = 0;
    bool m_limit_warning_logged = false;

    /* Most recent phase currents consumed by the FOC ISR (for telemetry). */
    float m_last_iu_a = 0.0f;
    float m_last_iv_a = 0.0f;
    float m_last_iw_a = 0.0f;
};

/**
 * @brief Global FOC manager instance.
 */
FocControlManager& focControlManager();

} // namespace Inverter
