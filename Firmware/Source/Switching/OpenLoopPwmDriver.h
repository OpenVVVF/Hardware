// OpenLoopPwmDriver.h
#pragma once

#include <cstdint>
#include <cmath>

#include "pico/stdlib.h"
#include "ThreePhasePwmBridge.h"
#include "Hardware.h" // only used here for your current board defaults

// ============================================================================
// Modulation Strategy Interface (UNCHANGED from your original)
// ============================================================================
class ModulationStrategy {
public:
    virtual ~ModulationStrategy() = default;

    // Compute duty cycles (0..top) for three phases
    virtual void computeDuties(float theta, float mod_index, uint16_t top,
                               uint16_t& duty_u, uint16_t& duty_v, uint16_t& duty_w) = 0;

    virtual const char* getName() const = 0;
};

// ============================================================================
// SPWM Implementation (UNCHANGED behavior)
// ============================================================================
class SPWMStrategy : public ModulationStrategy {
public:
    void computeDuties(float theta, float mod_index, uint16_t top,
                       uint16_t& duty_u, uint16_t& duty_v, uint16_t& duty_w) override;
    const char* getName() const override { return "SPWM"; }
};

// ============================================================================
// OpenLoopPwmDriver
// - “Layer on top” that preserves your original high-level features:
//   frequency ramping, auto MI curve, sync/async phase step, enable/disable,
//   modulation strategy hot-swap.
// - Uses ThreePhasePwmBridge underneath.
// ============================================================================
class OpenLoopPwmDriver {
public:
    struct Config {
        ThreePhasePwmBridge::Config bridge;

        // Keep your current defaults/behavior:
        bool auto_modulation = true;
        float auto_mi_min = 0.04f;
        float auto_mi_max = 0.99f;
        float auto_mi_full_freq_hz = 120.0f;

        Config() {
            // Board defaults (your current pins/limits)
            bridge.phase[0] = { Hardware::Pins::U_A, Hardware::Pins::U_B };
            bridge.phase[1] = { Hardware::Pins::V_A, Hardware::Pins::V_B };
            bridge.phase[2] = { Hardware::Pins::W_A, Hardware::Pins::W_B };

            bridge.carrier_min_hz = Hardware::Limits::Switching::MIN_HZ;
            bridge.carrier_max_hz = Hardware::Limits::Switching::MAX_HZ;

            // Keep bootstrap clamp defaults as before
            bridge.min_duty_percent = 1.0f;
            bridge.max_duty_percent = 99.0f;
        }
    };

    explicit OpenLoopPwmDriver(const Config& cfg);

    void init(float initial_carrier_hz = 2000.0f);

    // Strategy
    void setStrategy(ModulationStrategy* strategy);
    ModulationStrategy* getStrategy() const { return strategy_; }

    // Carrier
    void setCarrierFrequency(float hz);
    float getCarrierFrequency() const { return bridge_.getCarrierFrequency(); }

    // Frequency control
    void setTargetFrequency(float hz, float ramp_rate_hz_per_sec = 100.0f);
    void setFrequencyImmediate(float hz);
    float getCurrentFrequency() const { return current_freq_; }
    float getTargetFrequency() const { return target_freq_; }

    // Modulation
    void setModulationIndex(float mi);
    float getModulationIndex() const { return mod_index_; }
    void setAutoModulation(bool enable);

    // Sync mode
    void setSynchronousMode(bool enable, uint16_t pulses_per_cycle = 0);
    bool isSynchronousMode() const { return sync_mode_; }
    uint16_t getPulsesPerCycle() const { return pulses_per_cycle_; }

    // Control
    void enable();
    void disable();
    void emergencyStop();
    void clearEmergency();

    bool isEnabled() const { return enabled_; }
    bool isEmergencyStopped() const { return bridge_.isEmergencyStopped(); }

    // Main loop tick
    void update(float dt_seconds);

    // Expose bridge if needed (e.g. for diagnostics)
    ThreePhasePwmBridge& bridge() { return bridge_; }
    const ThreePhasePwmBridge& bridge() const { return bridge_; }

private:
    Config config_;
    ThreePhasePwmBridge bridge_;
    ModulationStrategy* strategy_ = nullptr;

    // state
    bool enabled_ = false;
    bool sync_mode_ = false;
    bool auto_modulation_ = true;

    float theta_ = 0.0f;
    float dtheta_ = 0.0f;

    float current_freq_ = 0.0f;
    float target_freq_ = 0.0f;
    float ramp_rate_ = 100.0f;

    uint16_t pulses_per_cycle_ = 0;
    float mod_index_ = 0.0f;

    static void wrapThunk_(void* user);
    void onPwmWrap_();
    void updatePhaseStep_();
};
