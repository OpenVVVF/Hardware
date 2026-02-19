/**
 ***********************************************************************************
 * @file    PWMDriver.cpp
 * @date    2026-02-15
 * @brief   Implementation of the RP2040 3-Phase PWM Driver.
 ***********************************************************************************
 */

#include "PWMDriver.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "Hardware.h"

#include <cmath>
#include <algorithm>

PWMDriver* PWMDriver::instance_ = nullptr;

// Define pins here (so header doesn’t need Hardware.h)
PWMDriver::PhasePins PWMDriver::kPins[PWMDriver::kPhaseCount] = {
    {Hardware::Pins::U_A, Hardware::Pins::U_B},
    {Hardware::Pins::V_A, Hardware::Pins::V_B},
    {Hardware::Pins::W_A, Hardware::Pins::W_B},
};

/**
 * @brief C-compatible ISR wrapper that calls the class member handler.
 */
extern "C" void pwm_wrap_isr() {
    if (PWMDriver::instance()) {
        PWMDriver::instance()->isrHandler();
    }
}

/**
 * @brief Clamps a float value between 0.0 and 1.0.
 */
static inline float clamp(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

void PWMDriver::setDutyCycles(float du, float dv, float dw) {
    // 1. Normalize inputs to 0.0 - 1.0 range
    du = clamp(du);
    dv = clamp(dv);
    dw = clamp(dw);

    // 2. Convert float duty to integer counter counts [0..pwm_top_]
    uint16_t cu = (uint16_t)lroundf(du * (float)pwm_top_);
    uint16_t cv = (uint16_t)lroundf(dv * (float)pwm_top_);
    uint16_t cw = (uint16_t)lroundf(dw * (float)pwm_top_);

    // 3. Enforce configured min/max bounds (e.g., for bootstrap charging or dead-time simulation)
    cu = clampDuty(cu, min_count_, max_count_);
    cv = clampDuty(cv, min_count_, max_count_);
    cw = clampDuty(cw, min_count_, max_count_);

    // 4. Update shadow registers used by the ISR.
    // Disable IRQ briefly to ensure atomicity of the 3 updates.
    irq_set_enabled(PWM_IRQ_WRAP, false);
    manual_du_ = cu;
    manual_dv_ = cv;
    manual_dw_ = cw;
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void PWMDriver::recalcDutyLimits() {
    // Precompute clamp counts in integer math based on current PWM TOP value.
    uint32_t lo = (static_cast<uint32_t>(pwm_top_) *
                   static_cast<uint32_t>(config_.min_duty_percent)) / 100u;
    uint32_t hi = (static_cast<uint32_t>(pwm_top_) *
                   static_cast<uint32_t>(config_.max_duty_percent)) / 100u;

    // Sanity checks to ensure limits stay within valid counter range
    if (lo > pwm_top_) lo = pwm_top_;
    if (hi > pwm_top_) hi = pwm_top_;
    if (hi < lo) hi = lo;

    min_count_ = static_cast<uint16_t>(lo);
    max_count_ = static_cast<uint16_t>(hi);
}

// ============================================================================
// PWMDriver Implementation
// ============================================================================

PWMDriver::PWMDriver(const Config& cfg) : config_(cfg) {
    instance_ = this;
}

void PWMDriver::init(float initial_carrier_hz) {
    // Calculate a clock divider that allows the minimum required frequency to fit in 16 bits.
    chooseFixedDivider(Hardware::Limits::Switching::MIN_HZ);

    // Apply the initial frequency
    setCarrierFrequency(initial_carrier_hz);
    
    // Ensure safe starting state
    forceAllGpioLow();
}

void PWMDriver::setCarrierFrequency(float hz) {
    // Clamp frequency to hardware limits
    if (hz < Hardware::Limits::Switching::MIN_HZ) hz = Hardware::Limits::Switching::MIN_HZ;
    if (hz > Hardware::Limits::Switching::MAX_HZ) hz = Hardware::Limits::Switching::MAX_HZ;

    carrier_hz_ = hz;

    // Calculate the new TOP register value for the target frequency
    uint16_t new_top = computeTopFromCarrier(carrier_hz_);

    if (enabled_) {
        // Update hardware TOP register.
        // Note: We disable the IRQ to prevent the ISR from running while we change TOP,
        // ensuring consistency across slices.
        irq_set_enabled(PWM_IRQ_WRAP, false);
        for (uint i = 0; i < kPhaseCount; ++i) {
            pwm_set_wrap(kSlices[i], new_top);
        }
        irq_set_enabled(PWM_IRQ_WRAP, true);
    } 
    
    // Store state and recalculate duty limits (since limits depend on TOP)
    pwm_top_ = new_top;
    recalcDutyLimits();
}

void PWMDriver::chooseFixedDivider(float min_carrier_hz) {
    const uint32_t sys_hz = clock_get_hz(clk_sys);

    // To maintain 16-bit resolution (TOP <= 65535) even at the lowest frequency:
    // top = sys / (2 * min_hz * div) - 1  <= 65535
    // Rearranging for divider (div):
    // div >= sys / (2 * min_hz * (65535+1))
    const double denom = 2.0 * (double)min_carrier_hz * 65536.0;
    double required = (double)sys_hz / denom;

    // Clamp divider to hardware valid range [1.0, 255.0]
    if (required < 1.0) required = 1.0;
    if (required > 255.0) required = 255.0;

    // RP2040 clock divider has 1/256 (approx 1/16 in integer mode?) fractional resolution.
    // Actually 8.4 fractional bits -> 1/16 step resolution.
    double q = ceil(required * 16.0) / 16.0;
    if (q > 255.0) q = 255.0;

    fixed_clk_div_ = (float)q;
    clk_div_ = fixed_clk_div_; // Apply to the active divider
}

uint16_t PWMDriver::computeTopFromCarrier(float carrier_hz) const {
    const uint32_t sys_hz = clock_get_hz(clk_sys);

    // Safety clamps
    if (carrier_hz < Hardware::Limits::Switching::MIN_HZ) carrier_hz = Hardware::Limits::Switching::MIN_HZ;
    if (carrier_hz > Hardware::Limits::Switching::MAX_HZ) carrier_hz = Hardware::Limits::Switching::MAX_HZ;

    // Calculate TOP: Top = (clk / (div * 2 * freq)) - 1
    double top = ((double)sys_hz / (2.0 * (double)carrier_hz * (double)fixed_clk_div_)) - 1.0;
    
    if (top < 0.0) top = 0.0;
    if (top > 65535.0) top = 65535.0;
    return (uint16_t)llround(top);
}

void PWMDriver::enable() {
    if (emergency_stop_) return;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, clk_div_);
    pwm_config_set_wrap(&cfg, pwm_top_);
    pwm_config_set_phase_correct(&cfg, false); // Start edge-aligned for easier sync

    // Initialize slices for each phase
    for (uint i = 0; i < kPhaseCount; ++i) {
        uint gpio_a = kPins[i].a;
        uint gpio_b = kPins[i].b;
        uint slice = pwm_gpio_to_slice_num(gpio_a);

        gpio_set_function(gpio_a, GPIO_FUNC_PWM);
        gpio_set_function(gpio_b, GPIO_FUNC_PWM);

        pwm_init(slice, &cfg, false);

        // Set complementary output: invert the B channel.
        // NOTE: RP2040 does not have hardware dead-time insertion. 
        // This inversion creates complementary PWM, but dead-time must be handled 
        // externally or by limited duty cycle (min_duty_percent).
        pwm_set_output_polarity(slice, false, true);

        // Start at 50% duty
        pwm_set_both_levels(slice, pwm_top_ / 2, pwm_top_ / 2);
    }

    // Synchronization routine:
    // 1. Reset counters to 0.
    for (uint i = 0; i < kPhaseCount; ++i) {
        pwm_set_counter(kSlices[i], 0);
    }

    // 2. Enable slices briefly in edge-aligned mode to align phases.
    pwm_set_mask_enabled((1u << kSlices[0]) | (1u << kSlices[1]) | (1u << kSlices[2]));
    sleep_us(50);
    pwm_set_mask_enabled(0);

    // 3. Switch to phase-correct (center-aligned) mode.
    // This reduces harmonic distortion compared to edge-aligned.
    for (uint i = 0; i < kPhaseCount; ++i) {
        pwm_set_phase_correct(kSlices[i], true);
    }

    // 4. Reset counters and restart.
    for (uint i = 0; i < kPhaseCount; ++i) {
        pwm_set_counter(kSlices[i], 0);
    }
    pwm_set_mask_enabled((1u << kSlices[0]) | (1u << kSlices[1]) | (1u << kSlices[2]));

    // Enable interrupt on wrap for Slice 0 to update duty cycles
    pwm_clear_irq(kSlices[0]);
    pwm_set_irq_enabled(kSlices[0], true);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    enabled_ = true;
}

void PWMDriver::disable() {
    enabled_ = false;
    pwm_set_mask_enabled(0); // Stop PWM hardware
    forceAllGpioLow();      // Force pins low for safety
}

void PWMDriver::emergencyStop() {
    emergency_stop_ = true;
    disable();
}

void PWMDriver::clearEmergency() {
    emergency_stop_ = false;
    restorePwmPins();
    sleep_us(10);
    enable();
}

void PWMDriver::isrHandler() {
    // Clear interrupt flag for the slice triggering the ISR (Slice 0)
    pwm_clear_irq(kSlices[0]);

    // Update timing stats
    pwm_wrap_count_++;
    last_wrap_time_us_ = time_us_32();

    if (emergency_stop_ || !enabled_) return;

    // Update the duty cycles from the shadow registers (manual_du_*)
    // This ensures the duty cycle changes apply synchronously to the PWM period.
    setSliceComplementary(kSlices[0], manual_du_);
    setSliceComplementary(kSlices[1], manual_dv_);
    setSliceComplementary(kSlices[2], manual_dw_);
}

void PWMDriver::forceAllGpioLow() {
    // Helper lambda to configure GPIO as standard output and drive low
    auto force_low = [](uint gpio) {
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, 0);
    };

    for (uint i = 0; i < kPhaseCount; ++i) {
        force_low(kPins[i].a);
        force_low(kPins[i].b);
    }
}

void PWMDriver::restorePwmPins() {
    // Restore GPIO function to PWM
    for (uint i = 0; i < kPhaseCount; ++i) {
        gpio_set_function(kPins[i].a, GPIO_FUNC_PWM);
        gpio_set_function(kPins[i].b, GPIO_FUNC_PWM);
    }
}

void PWMDriver::SetHardwareCommand(HardwareCommand _Cmd) {

    setCarrierFrequency(_Cmd.SwitchingFrequency_Hz);
    setDutyCycles(_Cmd.DutyPhU_unitless, _Cmd.DutyPhV_unitless, _Cmd.DutyPhW_unitless);
}