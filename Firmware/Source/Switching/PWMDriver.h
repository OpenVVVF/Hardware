/**
 ***********************************************************************************
 * @file    PWMDriver.h
 * @date    2026-02-15
 * @brief   RP2040 3-Phase PWM Driver with center-aligned (phase-correct) output.
 ***********************************************************************************
 */

#pragma once

#include "hardware/pwm.h"
#include <cstdint>


// AIDAN PLEASE MAKE THE PWMDRIVER SUPPORT THIS!
struct HardwareCommand {
    float SwitchingFrequency_Hz;
    float DutyPhU_unitless;
    float DutyPhV_unitless;
    float DutyPhW_unitless;
    
};


/**
 * @brief Handles generation of 3-phase complementary PWM signals for motor control.
 * 
 * This class manages the RP2040 PWM hardware to generate center-aligned PWM 
 * on three pairs of GPIO pins. It includes safety features such as emergency 
 * stop, hardware limits on duty cycle, and frequency configuration.
 */
class PWMDriver {
public:
    /**
     * @brief Configuration structure for PWM limits.
     */
    struct Config {
        float min_duty_percent = 2.0f; ///< Minimum duty cycle percentage (e.g., for bootstrap charging).
        float max_duty_percent = 98.0f; ///< Maximum duty cycle percentage (e.g., to prevent shoot-through).
    };

    /**
     * @brief Constructor.
     * @param cfg Configuration reference containing duty limits.
     */
    explicit PWMDriver(const Config& cfg);

    volatile uint32_t pwm_wrap_count_ = 0;  ///< Monotonic counter of PWM wrap events (used for timing).
    volatile uint32_t last_wrap_time_us_ = 0; ///< Timestamp of the last PWM wrap event in microseconds.
    bool isEnabled() const { return enabled_; }
    bool isEmergencyStopped() const { return emergency_stop_; }
    float getCarrierFrequency() const { return carrier_hz_; }

    /**
     * @brief Initializes the PWM hardware.
     * @param initial_carrier_hz The starting PWM frequency in Hz.
     */
    void init(float initial_carrier_hz);

    /**
     * @brief Updates the PWM carrier frequency.
     * @param hz New frequency in Hz.
     */
    void setCarrierFrequency(float hz);

    /**
     * @brief Sets the duty cycles for all three phases.
     * @param du Duty cycle for Phase U (0.0 to 1.0).
     * @param dv Duty cycle for Phase V (0.0 to 1.0).
     * @param dw Duty cycle for Phase W (0.0 to 1.0).
     */
    void setDutyCycles(float du, float dv, float dw);


    /**
    @brief NAUGHTY METHOD FIX THIS LATER ITS A VERY BAD BOY
    */
    void SetHardwareCommand(HardwareCommand _Cmd);

    void enable();
    void disable();

    void emergencyStop();
    void clearEmergency();

    /**
     * @brief Interrupt Service Routine handler called on PWM wrap.
     */
    void isrHandler();

    static PWMDriver* instance() { return instance_; }

private:
    /**
     * @brief Internal structure for timing calculations.
     */
    struct PwmTiming {
        float clk_div;
        uint16_t top;
    };

    static PWMDriver* instance_; ///< Singleton instance pointer.

    // ----- constants -----
    static constexpr float TWO_PI = 6.2831853071795864769f;

    static constexpr uint kPhaseCount = 3; ///< Number of motor phases.
    static constexpr uint kSlices[kPhaseCount] = {0, 1, 2}; ///< RP2040 PWM slice numbers used.

    struct PhasePins { uint a; uint b; }; ///< Structure holding high/low side pins for a phase.
    static PhasePins kPins[kPhaseCount]; ///< Pin mapping array {U_A, U_B}, {V_A, V_B}, etc.

    void recalcDutyLimits(); ///< Recalculates integer count limits based on current Top and percentage config.

    uint16_t computeTopFromCarrier(float carrier_hz) const;
    void chooseFixedDivider(float min_carrier_hz);

    static inline uint16_t clampDuty(uint16_t x, uint16_t lo, uint16_t hi) {
        return (x < lo) ? lo : (x > hi) ? hi : x;
    }

    static inline int16_t floatToQ15Clamp01(float x);

    /**
     * @brief Helper to set duty cycle on both channels of a slice (Complementary mode).
     * @param slice The PWM slice number.
     * @param level The compare level (duty cycle count).
     */
    inline void setSliceComplementary(uint slice, uint16_t level) {
        pwm_set_both_levels(slice, level, level);
    }

    void forceAllGpioLow();  ///< Forces all PWM GPIOs low as a safety measure.
    void restorePwmPins();   ///< Restores GPIO function to PWM after safety stop.

private:
    Config config_;
    
    float fixed_clk_div_ = 1.0f; ///< Calculated clock divider to ensure 16-bit counter range.
    bool enabled_ = false;
    bool emergency_stop_ = false;
    
    // Shadow registers updated by main thread, read by ISR
    volatile uint16_t manual_du_ = 0;
    volatile uint16_t manual_dv_ = 0;
    volatile uint16_t manual_dw_ = 0;

    // carrier / PWM timing
    float carrier_hz_ = 0.0f;
    float clk_div_ = 1.0f;
    uint16_t pwm_top_ = 0;

    // precomputed duty limits in counts
    uint16_t min_count_ = 0;
    uint16_t max_count_ = 0;
};