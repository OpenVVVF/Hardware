// ThreePhasePwmBridge.h
#pragma once

#include <cstdint>
#include <cmath>

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

// ============================================================================
// ThreePhasePwmBridge
// - Generic 3-phase PWM bridge driver for RP2040
// - Owns PWM slices/pins, sets carrier, enables/disables, and applies voltages.
// - Does NOT know anything about theta/frequency/modulation strategies.
// - Optional: provides a wrap callback for users who want PWM-synchronous control.
// ============================================================================
class ThreePhasePwmBridge {
public:
    static constexpr uint kNumPhases = 3;

    struct PhasePins {
        uint gpio_a = 0; // must be one PWM output of a slice
        uint gpio_b = 0; // must be the other PWM output of the same slice
    };

    struct Config {
        PhasePins phase[kNumPhases];

        // Output configuration
        bool center_aligned = true;         // phase-correct PWM
        bool invert_chan_a = false;         // output polarity for channel A
        bool invert_chan_b = true;          // output polarity for channel B (complementary default)

        // Safety limits (bootstrap caps, etc.)
        float min_duty_percent = 1.0f;      // clamp away from 0%
        float max_duty_percent = 99.0f;     // clamp away from 100%

        // Carrier guardrails (generic defaults; you can override)
        float carrier_min_hz = 10.0f;
        float carrier_max_hz = 100000.0f;

        // Future expansion
        uint deadtime_cycles = 0;

        // Which phase slice generates PWM wrap IRQ (0..2)
        uint irq_phase = 0;
    };

    using WrapCallback = void(*)(void* user);

    explicit ThreePhasePwmBridge(const Config& cfg);

    // Setup: computes top/div for initial carrier and forces outputs low.
    void init(float initial_carrier_hz = 2000.0f);

    // Carrier (updates hardware if enabled)
    void setCarrierFrequency(float hz);
    float getCarrierFrequency() const { return carrier_hz_; }

    // Hardware status
    uint16_t getTop() const { return pwm_top_; }
    float getClkDiv() const { return clk_div_; }
    bool isEnabled() const { return enabled_; }
    bool isEmergencyStopped() const { return emergency_stop_; }

    // Power stage control
    void enable();
    void disable();
    void emergencyStop();
    void clearEmergency(bool reenable = true);

    // ------------------------------------------------------------------------
    // Voltage / duty interface
    // ------------------------------------------------------------------------
    // Per-unit phase voltage command in [-1..+1], mapped to duty around 50%.
    // vu = -1 => 0% (then clamped by min_duty_percent)
    // vu =  0 => 50%
    // vu = +1 => 100% (then clamped by max_duty_percent)
    void setPhaseVoltagesPU(float vu, float vv, float vw);

    // Convenience: volts + bus voltage -> per-unit (VU / (Vbus/2)).
    void setPhaseVoltagesVolts(float vu, float vv, float vw, float vbus_volts);

    // Raw duty ticks [0..top] for each phase (still safety-clamped).
    void setDutyTicks(uint16_t du, uint16_t dv, uint16_t dw);

    // ------------------------------------------------------------------------
    // Optional PWM wrap callback (for FOC ISR triggering, modulators, etc.)
    // ------------------------------------------------------------------------
    void setWrapCallback(WrapCallback cb, void* user);
    void enableWrapIRQ(bool enable);

    // ISR handler called by the global C wrapper
    void isrHandler();

    // Singleton accessor used by the C ISR wrapper
    static ThreePhasePwmBridge* irqOwner() { return irq_owner_; }

    // Low-level pin forcing (useful for fault handling)
    void forceAllGpioLow();
    void restorePwmPins();

private:
    Config config_;

    // Derived hardware mapping
    uint slice_[kNumPhases]{};
    uint32_t slice_mask_ = 0;
    uint irq_slice_ = 0;

    // PWM parameters
    uint16_t pwm_top_ = 0;
    float clk_div_ = 1.0f;
    float carrier_hz_ = 2000.0f;

    // State
    bool enabled_ = false;
    bool emergency_stop_ = false;

    // Wrap callback
    bool wrap_irq_enabled_ = false;
    WrapCallback wrap_cb_ = nullptr;
    void* wrap_user_ = nullptr;

    // Singleton for IRQ ownership
    static ThreePhasePwmBridge* irq_owner_;

    // Helpers
    void computeTopDivForCarrier_(float carrier_hz, uint16_t& top_out, float& div_out) const;
    void updateHardwareClock_(float carrier_hz);
    void applyDutyToSlice_(uint phase_index, uint16_t level_ticks);
    uint16_t clampDuty_(uint16_t x) const;

    void configureWrapIRQ_(bool enable);
};

// Global C ISR wrapper (must be in a .cpp translation unit once)
extern "C" void pwm_wrap_isr();
