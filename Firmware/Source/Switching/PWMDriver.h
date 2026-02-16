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
// Field-Oriented Control (FOC) – sensor-based PMSM/BLDC current control
//
// This implementation assumes:
//   * You provide rotor mechanical angle (rad) from an absolute sensor (sin/cos).
//   * You provide at least 2 phase currents (the 3rd can be reconstructed).
//   * Control is executed on the PWM ISR core, using a lock-free snapshot reader.
//
// Notes:
//   * This is a pragmatic “bring-up” FOC loop: PI current controllers + simple
//     SVPWM via zero-sequence (common-mode) injection.
//   * Motor parameters (Ld/Lq/psi) decoupling is intentionally omitted to keep
//     bring-up safe; add feedforward once the loop is stable.
// ============================================================================

struct FocMeasurement {
    uint32_t t_us = 0;           // time_us_32() when sample was captured
    float i_u = 0.0f;            // phase U current [A]
    float i_v = 0.0f;            // phase V current [A]
    float i_w = 0.0f;            // phase W current [A]
    float v_bus = 0.0f;          // DC bus voltage [V]
    float theta_mech_rad = 0.0f; // mechanical rotor angle [rad], 0..2pi
};

using FocMeasReadFn = bool (*)(FocMeasurement* out);

class PWMDriver; // fwd

class FOCStrategy : public ModulationStrategy {
public:
    FOCStrategy();

    void setMeasReader(FocMeasReadFn fn) { meas_read_ = fn; }
    void attachDriver(PWMDriver* d) { driver_ = d; }

    // Configuration (safe defaults; tune on the bench)
    void setPolePairs(uint8_t pole_pairs) { pole_pairs_ = (pole_pairs == 0 ? 1 : pole_pairs); }
    void setElectricalOffsetRad(float rad) { elec_offset_rad_ = rad; }
    // Auto-calibration: align rotor at startup to discover electrical offset.
    // Intended for bench/dev use on a free-spinning motor.
    void setAutoCalEnabled(bool en) { auto_cal_enabled_ = en; }
    void clearCalibration();
    void setIqMax(float a) { iq_max_ = (a < 0 ? -a : a); }
    void setIdRef(float a) { id_ref_ = a; }
    void setCurrentGains(float kp, float ki) { id_pi_kp_ = kp; id_pi_ki_ = ki; iq_pi_kp_ = kp; iq_pi_ki_ = ki; }
    void setSpeedGains(float kp, float ki) { spd_pi_kp_ = kp; spd_pi_ki_ = ki; }

    // Compute duty cycles (0..top) for three phases.
    // theta/mod_index args are ignored; they exist for interface compatibility.
    void computeDuties(float theta, float mod_index, uint16_t top,
                      uint16_t& duty_u, uint16_t& duty_v, uint16_t& duty_w) override;
    const char* getName() const override { return "FOC"; }

private:
    // Snapshot source
    FocMeasReadFn meas_read_ = nullptr;
    PWMDriver* driver_ = nullptr;

    // Basic configuration
    uint8_t pole_pairs_ = 4;        // adjust for your motor
    float elec_offset_rad_ = 0.0f;  // electrical zero offset (sensor->phase alignment)
    bool  auto_cal_enabled_ = true;
    bool  calibrated_ = false;
    bool  invert_dir_ = false;

    enum class CalState : uint8_t { IDLE, ALIGN, VERIFY, RUN };
    CalState cal_state_ = CalState::IDLE;
    float cal_timer_s_ = 0.0f;
    float align_v_ = 4.0f;          // [V]
    float verify_vq_ = 3.0f;        // [V]
    float verify_start_theta_m_ = 0.0f;

    // References / limits
    float id_ref_ = 0.0f;           // [A]
    float iq_max_ = 60.0f;          // [A] torque current limit (bring-up)

    // PI gains (approx: V/A and V/(A*s) for current loops)
    float id_pi_kp_ = 0.05f;
    float id_pi_ki_ = 20.0f;
    float iq_pi_kp_ = 0.05f;
    float iq_pi_ki_ = 20.0f;

    // Speed loop PI gains (conservative)
    float spd_pi_kp_ = 0.0025f;
    float spd_pi_ki_ = 0.5f;

    // Integrators
    float id_int_ = 0.0f;
    float iq_int_ = 0.0f;
    float spd_int_ = 0.0f;

    // State
    bool have_prev_ = false;
    uint32_t prev_t_us_ = 0;
    float prev_theta_e_ = 0.0f;
    float omega_e_ = 0.0f;          // electrical rad/s (filtered)
    float speed_lp_ = 0.10f;        // speed low-pass factor
    float speed_loop_accum_ = 0.0f; // seconds

    // Helpers
    static float wrap_0_2pi(float a);
    static float angle_diff(float a, float b);
    static float clamp(float x, float lo, float hi);
    void resetControllers();

    void dutiesFromVab(float v_alpha, float v_beta, float vbus, uint16_t top,
                       uint16_t& duty_u, uint16_t& duty_v, uint16_t& duty_w);
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