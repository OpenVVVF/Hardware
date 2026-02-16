#pragma once

#include "hardware/pwm.h"
#include <cstdint>

class ModulationStrategy {
public:
    virtual ~ModulationStrategy() = default;

    // NEW: fixed-point friendly strategy hook (no floats required in ISR).
    // phase_q32: electrical angle in Q0.32 turns (0..2^32-1 is 0..360°)
    // mod_q15: modulation index in Q1.15 (0..32767 maps to 0..~1.0)
    virtual void computeDuties(uint32_t phase_q32,
                              int16_t mod_q15,
                              uint16_t top,
                              uint16_t& duty_u,
                              uint16_t& duty_v,
                              uint16_t& duty_w) = 0;
};

class SPWMStrategy final : public ModulationStrategy {
public:
    // LUT size must be power of two
    static constexpr uint32_t LUT_BITS = 10;
    static constexpr uint32_t LUT_SIZE = 1u << LUT_BITS;

    SPWMStrategy();

    void computeDuties(uint32_t phase_q32,
                       int16_t mod_q15,
                       uint16_t top,
                       uint16_t& duty_u,
                       uint16_t& duty_v,
                       uint16_t& duty_w) override;

private:
    static constexpr uint32_t INDEX_SHIFT = 32 - LUT_BITS;

    // Q0.32 offsets for +/-120 degrees (1/3 and 2/3 of a turn)
    static constexpr uint32_t PHASE_120 = 0x55555555u;
    static constexpr uint32_t PHASE_240 = 0xAAAAAAABu;

    // sine LUT scaled to int16 in [-32767..32767]
    int16_t sin_lut_[LUT_SIZE];

    static inline uint16_t dutyFromPhase(uint32_t phase_q32,
                                         int16_t mod_q15,
                                         uint16_t top,
                                         const int16_t* lut);
};

class PWMDriver {
public:
    struct Config {
        float min_duty_percent = 2.0f;
        float max_duty_percent = 98.0f;
    };

    explicit PWMDriver(const Config& cfg);
    float getCurrentFrequency() const { return current_freq_; }
    float getTargetFrequency() const { return target_freq_; }
    float getModulationIndex() const { return mod_index_; }
    bool isSynchronousMode() const { return sync_mode_; }
    uint16_t getPulsesPerCycle() const { return pulses_per_cycle_; }

    bool isEnabled() const { return enabled_; }
    bool isEmergencyStopped() const { return emergency_stop_; }
    float getCarrierFrequency() const { return carrier_hz_; }
    void init(float initial_carrier_hz);
    void setStrategy(ModulationStrategy* strategy);

    void setCarrierFrequency(float hz);

    void setTargetFrequency(float hz, float ramp_rate);
    void setFrequencyImmediate(float hz);

    void setModulationIndex(float mi);
    void setAutoModulation(bool enable);

    void setSynchronousMode(bool enable, uint16_t pulses_per_cycle);


    void setDutyCycles(float du, float dv, float dw);

    void enable();
    void disable();

    void emergencyStop();
    void clearEmergency();

    void update(float dt);

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

    void updatePhaseStep();

    void clearManualDuties();
    bool isManualDutyMode() const { return manual_duty_mode_; }
    // Disable manual mode (return to strategy-driven)

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

    ModulationStrategy* strategy_ = nullptr;
    float fixed_clk_div_ = 1.0f;     // static divider chosen at init
    bool enabled_ = false;
    bool emergency_stop_ = false;
    volatile bool manual_duty_mode_ = false;
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

    // frequency control
    float target_freq_ = 0.0f;
    float current_freq_ = 0.0f;
    float ramp_rate_ = 0.0f;

    // modulation
    bool auto_modulation_ = true;
    float mod_index_ = 0.0f;   // for API/debug only
    int16_t mod_q15_ = 0;      // for ISR math

    // phase stepping
    bool sync_mode_ = false;
    uint16_t pulses_per_cycle_ = 0;

    uint32_t phase_q32_ = 0;     // Q0.32 turns
    uint32_t phase_step_q32_ = 0;
};
