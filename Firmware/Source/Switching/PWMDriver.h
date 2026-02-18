#pragma once

#include "hardware/pwm.h"
#include <cstdint>

class PWMDriver {
public:
    struct Config {
        float min_duty_percent = 2.0f;
        float max_duty_percent = 98.0f;
    };

    explicit PWMDriver(const Config& cfg);
    volatile uint32_t pwm_wrap_count_ = 0;
    volatile uint32_t last_wrap_time_us_ = 0;

    bool isEnabled() const { return enabled_; }
    bool isEmergencyStopped() const { return emergency_stop_; }
    float getCarrierFrequency() const { return carrier_hz_; }
    void init(float initial_carrier_hz);

    void setCarrierFrequency(float hz);

    void setDutyCycles(float du, float dv, float dw);

    void enable();
    void disable();

    void emergencyStop();
    void clearEmergency();

    void isrHandler();

    static PWMDriver* instance() { return instance_; }

private:
    struct PwmTiming {
        float clk_div;
        uint16_t top;
    };

    static PWMDriver* instance_;

    // ----- constants -----
    static constexpr float TWO_PI = 6.2831853071795864769f;

    // If your slices truly are 0,1,2, keep this.
    // If not guaranteed, you can compute from GPIO->slice at init and store.
    static constexpr uint kPhaseCount = 3;
    static constexpr uint kSlices[kPhaseCount] = {0, 1, 2};

    struct PhasePins { uint a, b; };
    static PhasePins kPins[kPhaseCount];

    void recalcDutyLimits();


    uint16_t computeTopFromCarrier(float carrier_hz) const;
    void chooseFixedDivider(float min_carrier_hz);

    static inline uint16_t clampDuty(uint16_t x, uint16_t lo, uint16_t hi) {
        return (x < lo) ? lo : (x > hi) ? hi : x;
    }

    static inline int16_t floatToQ15Clamp01(float x);

    inline void setSliceComplementary(uint slice, uint16_t level) {
        // faster/cleaner than two pwm_set_chan_level calls
        pwm_set_both_levels(slice, level, level);
    }

    void forceAllGpioLow();
    void restorePwmPins();

private:
    Config config_;
    
    float fixed_clk_div_ = 1.0f;     // static divider chosen at init
    bool enabled_ = false;
    bool emergency_stop_ = false;
    volatile uint16_t manual_du_ = 0;
    volatile uint16_t manual_dv_ = 0;
    volatile uint16_t manual_dw_ = 0;
    // carrier / PWM timing
    float carrier_hz_ = 0.0f;
    float clk_div_ = 1.0f;
    uint16_t pwm_top_ = 0;

    // precomputed duty limits in counts (NO float in ISR)
    uint16_t min_count_ = 0;
    uint16_t max_count_ = 0;



};
