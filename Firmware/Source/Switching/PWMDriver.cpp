#include "PWMDriver.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "Hardware.h"

// Static instance pointer for ISR
PWMDriver* PWMDriver::instance_ = nullptr;

// C-compatible ISR wrapper
extern "C" void pwm_wrap_isr() {
    if (PWMDriver::instance()) {
        PWMDriver::instance()->isrHandler();
    }
}

// ============================================================================
// SPWM Implementation
// ============================================================================
void SPWMStrategy::computeDuties(float theta, float mod_index, uint16_t top,
                                uint16_t& duty_u, uint16_t& duty_v, uint16_t& duty_w) {
    // Three-phase sine calculation
    float su = sinf(theta);
    float sv = sinf(theta - 2.0f * static_cast<float>(M_PI) / 3.0f);
    float sw = sinf(theta + 2.0f * static_cast<float>(M_PI) / 3.0f);
    
    // Apply modulation index and offset to [0, 1]
    auto toDuty = [&](float s) -> uint16_t {
        float x = (mod_index * s + 1.0f) * 0.5f;
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        return static_cast<uint16_t>(x * static_cast<float>(top));
    };
    
    duty_u = toDuty(su);
    duty_v = toDuty(sv);
    duty_w = toDuty(sw);
}

// ============================================================================
// PWMDriver Implementation
// ============================================================================
PWMDriver::PWMDriver(const Config& cfg) : config_(cfg) {
    // Set singleton (last constructed driver wins - don't create multiple instances)
    instance_ = this;
}

void PWMDriver::init(float initial_carrier_hz) {
    // Initialize slices but don't enable yet
    setCarrierFrequency(initial_carrier_hz);
    
    // Configure pins as GPIO (low) initially for safety
    forceAllGpioLow();
}

void PWMDriver::setStrategy(ModulationStrategy* strategy) {
    strategy_ = strategy;
}

void PWMDriver::setCarrierFrequency(float hz) {
    if (hz < Hardware::Limits::Switching::MIN_HZ) hz = Hardware::Limits::Switching::MIN_HZ;
    if (hz > Hardware::Limits::Switching::MAX_HZ) hz = Hardware::Limits::Switching::MAX_HZ;
    
    carrier_hz_ = hz;
    if (enabled_) {
        updateHardwareClock(hz);
        updatePhaseStep();
    } else {
        // Pre-calculate top value even if not running
        const uint32_t sys_hz = clock_get_hz(clk_sys);
        float ideal_top = (static_cast<float>(sys_hz) / (2.0f * hz)) - 1.0f;
        if (ideal_top > 65535.0f) {
            clk_div_ = ceilf((ideal_top / 65535.0f) * 10.0f) / 10.0f;
            if (clk_div_ > 255.0f) clk_div_ = 255.0f;
            pwm_top_ = static_cast<uint16_t>((static_cast<float>(sys_hz) / 
                      (2.0f * hz * clk_div_)) - 1.0f);
        } else {
            clk_div_ = 1.0f;
            pwm_top_ = static_cast<uint16_t>(ideal_top);
        }
    }
}

void PWMDriver::updateHardwareClock(float carrier_hz) {
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    float ideal_top = (static_cast<float>(sys_hz) / (2.0f * carrier_hz)) - 1.0f;
    float div = 1.0f;
    uint16_t new_top;
    
    if (ideal_top > 65535.0f) {
        div = ceilf((ideal_top / 65535.0f) * 10.0f) / 10.0f;
        if (div > 255.0f) div = 255.0f;
        new_top = static_cast<uint16_t>((static_cast<float>(sys_hz) / 
                                        (2.0f * carrier_hz * div)) - 1.0f);
    } else {
        new_top = static_cast<uint16_t>(ideal_top);
    }
    
    // Critical section: disable IRQ during hardware update
    irq_set_enabled(PWM_IRQ_WRAP, false);
    
    for (uint slice = 0; slice < 3; slice++) {
        pwm_set_clkdiv(slice, div);
        pwm_set_wrap(slice, new_top);
    }
    
    pwm_top_ = new_top;
    clk_div_ = div;
    
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void PWMDriver::setTargetFrequency(float hz, float ramp_rate) {
    target_freq_ = hz;
    ramp_rate_ = ramp_rate;
}

void PWMDriver::setFrequencyImmediate(float hz) {
    target_freq_ = hz;
    current_freq_ = hz;
    updatePhaseStep();
}

void PWMDriver::setModulationIndex(float mi) {
    mod_index_ = mi;
    auto_modulation_ = false;  // Manual override disables auto curve
}

void PWMDriver::setAutoModulation(bool enable) {
    auto_modulation_ = enable;
}

void PWMDriver::setSynchronousMode(bool enable, uint16_t pulses_per_cycle) {
    sync_mode_ = enable;
    pulses_per_cycle_ = pulses_per_cycle;
    updatePhaseStep();
}

void PWMDriver::updatePhaseStep() {
    if (sync_mode_ && std::fabs(current_freq_) > 0.01f && pulses_per_cycle_ > 0) {
        // Synchronous: fixed angle step per PWM cycle
        dtheta_ = 2.0f * static_cast<float>(M_PI) / static_cast<float>(pulses_per_cycle_);
    } else {
        // Asynchronous: continuous phase rotation
        if (carrier_hz_ > 0) {
            dtheta_ = 2.0f * static_cast<float>(M_PI) * current_freq_ / carrier_hz_;
        } else {
            dtheta_ = 0;
        }
    }
}

void PWMDriver::enable() {
    if (emergency_stop_) return;
    
    // Initialize PWM hardware
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, clk_div_);
    pwm_config_set_wrap(&cfg, pwm_top_);
    pwm_config_set_phase_correct(&cfg, false);  // Start as edge-aligned
    
    // Initialize all three half-bridges
    auto init_slice = [&](uint gpio_a, uint gpio_b) {
        uint slice = pwm_gpio_to_slice_num(gpio_a);
        gpio_set_function(gpio_a, GPIO_FUNC_PWM);
        gpio_set_function(gpio_b, GPIO_FUNC_PWM);
        pwm_init(slice, &cfg, false);
        // Complementary output: B inverted relative to A
        pwm_set_output_polarity(slice, false, true);
        pwm_set_chan_level(slice, PWM_CHAN_A, pwm_top_ / 2);
        pwm_set_chan_level(slice, PWM_CHAN_B, pwm_top_ / 2);
    };
    
    init_slice(Hardware::Pins::U_A, Hardware::Pins::U_B);
    init_slice(Hardware::Pins::V_A, Hardware::Pins::V_B);
    init_slice(Hardware::Pins::W_A, Hardware::Pins::W_B);
    
    // Sync procedure: align counters
    pwm_set_counter(0, 0);
    pwm_set_counter(1, 0);
    pwm_set_counter(2, 0);
    
    // Brief enable to sync, then switch to phase-correct
    pwm_set_mask_enabled(PWM_SLICE_MASK);
    sleep_us(50);
    pwm_set_mask_enabled(0);
    
    // Switch to phase-correct (center-aligned) for better waveform
    pwm_set_phase_correct(0, true);
    pwm_set_phase_correct(1, true);
    pwm_set_phase_correct(2, true);
    
    // Reset counters and restart
    pwm_set_counter(0, 0);
    pwm_set_counter(1, 0);
    pwm_set_counter(2, 0);
    pwm_set_mask_enabled(PWM_SLICE_MASK);
    
    // Setup interrupt
    pwm_clear_irq(0);
    pwm_set_irq_enabled(0, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    
    enabled_ = true;
    theta_ = 0.0f;
    updatePhaseStep();
}

void PWMDriver::disable() {
    enabled_ = false;
    pwm_set_mask_enabled(0);
    forceAllGpioLow();
    target_freq_ = 0.0f;
    current_freq_ = 0.0f;
    theta_ = 0.0f;
}

void PWMDriver::emergencyStop() {
    emergency_stop_ = true;
    enabled_ = false;
    pwm_set_mask_enabled(0);
    forceAllGpioLow();
    target_freq_ = 0.0f;
    current_freq_ = 0.0f;
    theta_ = 0.0f;
}

void PWMDriver::clearEmergency() {
    emergency_stop_ = false;
    restorePwmPins();
    sleep_us(10);
    enable();
}

void PWMDriver::update(float dt) {
    if (emergency_stop_) return;
    
    // Frequency ramping
    float err = target_freq_ - current_freq_;
    if (std::fabs(err) > 0.01f) {
        float step = ramp_rate_ * dt;
        if (err > 0) {
            current_freq_ += (err > step ? step : err);
        } else {
            current_freq_ -= (-err > step ? step : -err);
        }
        updatePhaseStep();
    } else {
        current_freq_ = target_freq_;  // Snap to target
    }
    
    // Auto-disable when ramped to zero
    if (enabled_ && target_freq_ == 0.0f && std::fabs(current_freq_) < 0.01f) {
        disable();
        return;
    }
    
    // Auto-modulation curve (matches your original code)
    
    if (auto_modulation_) {
        float abs_freq = std::fabs(current_freq_);
        if (abs_freq <= 0.0f) {
            mod_index_ = 0.04f;  // Maintain minimum flux
        } else if (abs_freq >= 120.0f) {
            mod_index_ = 0.99f;  // Full modulation at high speed
        } else {
            // Linear ramp from 5% to 99% over 0-60Hz
            mod_index_ = 0.04f + (abs_freq / 120.0f) * (0.99f - 0.04f);
        }
    }
    
    // Enable PWM if we have a target but were disabled
    if (!enabled_ && std::fabs(target_freq_) > 0.01f && !emergency_stop_) {
        enable();
    }
}

void PWMDriver::isrHandler() {
    pwm_clear_irq(0);
    
    if (emergency_stop_ || !enabled_ || !strategy_) {
        return;
    }
    
    // Get duty values from modulation strategy
    uint16_t du, dv, dw;
    strategy_->computeDuties(theta_, mod_index_, pwm_top_, du, dv, dw);
    
    // Apply safety clamping (keep away from 0% and 100% for bootstrap)
    uint16_t lo = static_cast<uint16_t>(pwm_top_ * (config_.min_duty_percent / 100.0f));
    uint16_t hi = static_cast<uint16_t>(pwm_top_ * (config_.max_duty_percent / 100.0f));
    
    du = clampDuty(du, lo, hi);
    dv = clampDuty(dv, lo, hi);
    dw = clampDuty(dw, lo, hi);
    
    // Update hardware
    setSliceComplementary(0, du);
    setSliceComplementary(1, dv);
    setSliceComplementary(2, dw);
    
    // Advance electrical angle
    theta_ += dtheta_;
    if (theta_ >= 2.0f * static_cast<float>(M_PI)) {
        theta_ -= 2.0f * static_cast<float>(M_PI);
    }
    if (theta_ < 0.0f) {
        theta_ += 2.0f * static_cast<float>(M_PI);
    }
}

void PWMDriver::setSliceComplementary(uint slice, uint16_t level) {
    // Write same level to both channels. Polarity inversion on B channel 
    // (set during init) creates complementary pair with deadtime if hardware supports it
    pwm_set_chan_level(slice, PWM_CHAN_A, level);
    pwm_set_chan_level(slice, PWM_CHAN_B, level);
}

void PWMDriver::forceAllGpioLow() {
    auto force_low = [&](uint gpio) {
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, 0);
    };
    
    force_low(Hardware::Pins::U_A);
    force_low(Hardware::Pins::U_B);
    force_low(Hardware::Pins::V_A);
    force_low(Hardware::Pins::V_B);
    force_low(Hardware::Pins::W_A);
    force_low(Hardware::Pins::W_B);
}

void PWMDriver::restorePwmPins() {
    gpio_set_function(Hardware::Pins::U_A, GPIO_FUNC_PWM);
    gpio_set_function(Hardware::Pins::U_B, GPIO_FUNC_PWM);
    gpio_set_function(Hardware::Pins::V_A, GPIO_FUNC_PWM);
    gpio_set_function(Hardware::Pins::V_B, GPIO_FUNC_PWM);
    gpio_set_function(Hardware::Pins::W_A, GPIO_FUNC_PWM);
    gpio_set_function(Hardware::Pins::W_B, GPIO_FUNC_PWM);
}

uint16_t PWMDriver::clampDuty(uint16_t x, uint16_t lo, uint16_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}