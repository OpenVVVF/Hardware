#pragma once

#include <cstdint>
#include <cmath>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "Hardware.h"
// ============================================================================
// Modulation Strategy Interface
// Implement this to add SVM, FOC, SHE, etc.
// ============================================================================
class ModulationStrategy {
public:
    virtual ~ModulationStrategy() = default;
    
    // Compute duty cycles (0 to top) for three phases
    // theta: electrical angle [0, 2π]
    // mod_index: 0.0 to 1.0+ (modulation depth)
    // top: PWM counter top value (period)
    virtual void computeDuties(float theta, float mod_index, uint16_t top,
                              uint16_t& duty_u, uint16_t& duty_v, uint16_t& duty_w) = 0;
    
    virtual const char* getName() const = 0;
};

// ============================================================================
// Sine PWM Implementation (SPWM)
// ============================================================================
class SPWMStrategy : public ModulationStrategy {
public:
    void computeDuties(float theta, float mod_index, uint16_t top,
                      uint16_t& duty_u, uint16_t& duty_v, uint16_t& duty_w) override;
    const char* getName() const override { return "SPWM"; }
};

// ============================================================================
// Hardware Driver for 3-Phase Bridge
// ============================================================================
class PWMDriver {
public:
    struct Config {
        
        // Safety limits: keep duty away from 0% and 100% for bootstrap caps
        float min_duty_percent = 1.0f;   // 1% minimum
        float max_duty_percent = 99.0f;  // 99% maximum
        
        // Future expansion: deadtime in PWM clock cycles
        uint deadtime_cycles = 0;
    };

    explicit PWMDriver(const Config& cfg);
    
    // Setup PWM hardware. Call once at startup.
    void init(float initial_carrier_hz = 2000.0f);
    
    // ------------------------------------------------------------------------
    // Modulation Strategy (hot-swappable)
    // ------------------------------------------------------------------------
    void setStrategy(ModulationStrategy* strategy);
    ModulationStrategy* getStrategy() const { return strategy_; }
    
    // ------------------------------------------------------------------------
    // Carrier Frequency (automatically updates hardware)
    // ------------------------------------------------------------------------
    void setCarrierFrequency(float hz);
    float getCarrierFrequency() const { return carrier_hz_; }
    
    // ------------------------------------------------------------------------
    // Electrical Frequency Control (with optional ramping)
    // ------------------------------------------------------------------------
    void setTargetFrequency(float hz, float ramp_rate_hz_per_sec = 100.0f);
    void setFrequencyImmediate(float hz);  // Bypass ramp
    float getCurrentFrequency() const { return current_freq_; }
    float getTargetFrequency() const { return target_freq_; }
    
    // ------------------------------------------------------------------------
    // Modulation Index (auto-calculated from frequency unless overridden)
    // ------------------------------------------------------------------------
    void setModulationIndex(float mi);  // 0.0 to 1.0+
    float getModulationIndex() const { return mod_index_; }
    void setAutoModulation(bool enable); // If true, scales MI with frequency
    
    // ------------------------------------------------------------------------
    // Synchronous Mode (for high-frequency operation)
    // In sync mode: dtheta = 2π / pulses_per_cycle (locked to carrier)
    // In async mode: dtheta = 2π * f / carrier (free running)
    // ------------------------------------------------------------------------
    void setSynchronousMode(bool enable, uint16_t pulses_per_cycle = 0);
    bool isSynchronousMode() const { return sync_mode_; }
    uint16_t getPulsesPerCycle() const { return pulses_per_cycle_; }
    
    // ------------------------------------------------------------------------
    // Control Interface
    // ------------------------------------------------------------------------
    void enable();           // Soft start with synchronization
    void disable();          // Ramp down and stop (soft disable)
    void emergencyStop();    // Immediate hardware shutdown (force GPIO low)
    void clearEmergency();   // Reset from emergency state
    
    bool isEnabled() const { return enabled_; }
    bool isEmergencyStopped() const { return emergency_stop_; }
    
    // ------------------------------------------------------------------------
    // Main Loop Update (call at regular intervals, e.g., 5ms)
    // Handles frequency ramping and modulation index curves
    // ------------------------------------------------------------------------
    void update(float dt_seconds);
    
    // ------------------------------------------------------------------------
    // Low-level Access (for advanced users)
    // ------------------------------------------------------------------------
    void forceAllGpioLow();
    void restorePwmPins();
    uint16_t getTop() const { return pwm_top_; }
    
    // ------------------------------------------------------------------------
    // Interrupt Handler (internal use, but public for ISR registration)
    // ------------------------------------------------------------------------
    void isrHandler();
    
    // Singleton accessor for C ISR wrapper
    static PWMDriver* instance() { return instance_; }

private:
    Config config_;
    ModulationStrategy* strategy_ = nullptr;
    
    // Hardware state
    uint16_t pwm_top_ = 0;
    float clk_div_ = 1.0f;
    float carrier_hz_ = 2000.0f;
    static constexpr uint PWM_SLICE_MASK = (1u << 0) | (1u << 1) | (1u << 2);
    
    // Operating state
    bool enabled_ = false;
    bool emergency_stop_ = false;
    bool sync_mode_ = false;
    bool auto_modulation_ = true;  // Default to frequency-based MI curve
    
    // Electrical angle state
    float theta_ = 0.0f;
    float dtheta_ = 0.0f;
    
    // Frequency control
    float current_freq_ = 0.0f;
    float target_freq_ = 0.0f;
    float ramp_rate_ = 100.0f;
    uint16_t pulses_per_cycle_ = 0;
    
    // Modulation
    float mod_index_ = 0.0f;
    
    // Singleton instance for ISR
    static PWMDriver* instance_;
    
    // Internal methods
    void updateHardwareClock(float carrier_hz);
    void updatePhaseStep();
    void setSliceComplementary(uint slice, uint16_t level);
    static uint16_t clampDuty(uint16_t x, uint16_t lo, uint16_t hi);
    
    // Helper to get slice numbers from GPIO
    uint getSlice(uint gpio) const { return pwm_gpio_to_slice_num(gpio); }
    uint getChan(uint gpio) const { return pwm_gpio_to_channel(gpio); }
};