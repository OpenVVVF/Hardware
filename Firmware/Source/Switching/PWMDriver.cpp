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

extern "C" void pwm_wrap_isr() {
    if (PWMDriver::instance()) {
        PWMDriver::instance()->isrHandler();
    }
}

static inline float clamp(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

void PWMDriver::setDutyCycles(float du, float dv, float dw) {
    du = clamp(du);
    dv = clamp(dv);
    dw = clamp(dw);

    // Convert to counts [0..pwm_top_]
    uint16_t cu = (uint16_t)lroundf(du * (float)pwm_top_);
    uint16_t cv = (uint16_t)lroundf(dv * (float)pwm_top_);
    uint16_t cw = (uint16_t)lroundf(dw * (float)pwm_top_);

    // Enforce your configured min/max bounds
    cu = clampDuty(cu, min_count_, max_count_);
    cv = clampDuty(cv, min_count_, max_count_);
    cw = clampDuty(cw, min_count_, max_count_);

    // Make update coherent wrt ISR
    irq_set_enabled(PWM_IRQ_WRAP, false);
    manual_du_ = cu;
    manual_dv_ = cv;
    manual_dw_ = cw;
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void PWMDriver::recalcDutyLimits() {
    // Precompute clamp counts in integer math.
    // Clamp to valid [0..top] bounds.
    uint32_t lo = (static_cast<uint32_t>(pwm_top_) *
                   static_cast<uint32_t>(config_.min_duty_percent)) / 100u;
    uint32_t hi = (static_cast<uint32_t>(pwm_top_) *
                   static_cast<uint32_t>(config_.max_duty_percent)) / 100u;

    if (lo > pwm_top_) lo = pwm_top_;
    if (hi > pwm_top_) hi = pwm_top_;
    if (hi < lo) hi = lo;

    min_count_ = static_cast<uint16_t>(lo);
    max_count_ = static_cast<uint16_t>(hi);
}

// ============================================================================
// PWMDriver
// ============================================================================
PWMDriver::PWMDriver(const Config& cfg) : config_(cfg) {
    instance_ = this;
}

void PWMDriver::init(float initial_carrier_hz) {
    chooseFixedDivider(Hardware::Limits::Switching::MIN_HZ);

    setCarrierFrequency(initial_carrier_hz);
    forceAllGpioLow();
}


void PWMDriver::setCarrierFrequency(float hz) {
    if (hz < Hardware::Limits::Switching::MIN_HZ) hz = Hardware::Limits::Switching::MIN_HZ;
    if (hz > Hardware::Limits::Switching::MAX_HZ) hz = Hardware::Limits::Switching::MAX_HZ;

    carrier_hz_ = hz;

    uint16_t new_top = computeTopFromCarrier(carrier_hz_);

    if (enabled_) {
        // No blanking: TOP is double-buffered and latches on boundary.
        // Keep the 3 slices consistent by writing them together with wrap IRQ disabled.
        irq_set_enabled(PWM_IRQ_WRAP, false);
        for (uint i = 0; i < kPhaseCount; ++i) {
            pwm_set_wrap(kSlices[i], new_top);
        }
        irq_set_enabled(PWM_IRQ_WRAP, true);
    } 
    pwm_top_ = new_top;
    recalcDutyLimits();
    
}


void PWMDriver::chooseFixedDivider(float min_carrier_hz) {
    const uint32_t sys_hz = clock_get_hz(clk_sys);

    // Need: top <= 65535 at min_carrier_hz
    // top = sys / (2 * min_hz * div) - 1  <= 65535
    // => div >= sys / (2 * min_hz * (65535+1))
    const double denom = 2.0 * (double)min_carrier_hz * 65536.0;
    double required = (double)sys_hz / denom;

    if (required < 1.0) required = 1.0;
    if (required > 255.0) required = 255.0;

    // Quantize to 1/16 steps (RP2040 divider fractional resolution)
    double q = ceil(required * 16.0) / 16.0;
    if (q > 255.0) q = 255.0;

    fixed_clk_div_ = (float)q;
    clk_div_ = fixed_clk_div_; // driver uses clk_div_ elsewhere
}

uint16_t PWMDriver::computeTopFromCarrier(float carrier_hz) const {
    const uint32_t sys_hz = clock_get_hz(clk_sys);

    if (carrier_hz < Hardware::Limits::Switching::MIN_HZ) carrier_hz = Hardware::Limits::Switching::MIN_HZ;
    if (carrier_hz > Hardware::Limits::Switching::MAX_HZ) carrier_hz = Hardware::Limits::Switching::MAX_HZ;

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
    pwm_config_set_phase_correct(&cfg, false); // sync start edge-aligned

    // init slices using arrays
    for (uint i = 0; i < kPhaseCount; ++i) {
        uint gpio_a = kPins[i].a;
        uint gpio_b = kPins[i].b;
        uint slice = pwm_gpio_to_slice_num(gpio_a);

        gpio_set_function(gpio_a, GPIO_FUNC_PWM);
        gpio_set_function(gpio_b, GPIO_FUNC_PWM);

        pwm_init(slice, &cfg, false);

        // complementary output by polarity inversion (NOTE: RP2040 has no deadtime insertion)
        pwm_set_output_polarity(slice, false, true);

        // start mid-duty
        pwm_set_both_levels(slice, pwm_top_ / 2, pwm_top_ / 2);
    }

    // sync counters (assuming slices are 0,1,2; otherwise store actual slice nums)
    for (uint i = 0; i < kPhaseCount; ++i) {
        pwm_set_counter(kSlices[i], 0);
    }

    // brief enable to sync, then stop
    pwm_set_mask_enabled((1u << kSlices[0]) | (1u << kSlices[1]) | (1u << kSlices[2]));
    sleep_us(50);
    pwm_set_mask_enabled(0);

    // switch to phase-correct (center-aligned)
    for (uint i = 0; i < kPhaseCount; ++i) {
        pwm_set_phase_correct(kSlices[i], true);
    }

    // reset counters and restart
    for (uint i = 0; i < kPhaseCount; ++i) {
        pwm_set_counter(kSlices[i], 0);
    }
    pwm_set_mask_enabled((1u << kSlices[0]) | (1u << kSlices[1]) | (1u << kSlices[2]));

    // IRQ on slice 0 wrap
    pwm_clear_irq(kSlices[0]);
    pwm_set_irq_enabled(kSlices[0], true);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    enabled_ = true;
}

void PWMDriver::disable() {
    enabled_ = false;
    pwm_set_mask_enabled(0);
    forceAllGpioLow();
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
    pwm_clear_irq(kSlices[0]);
    pwm_wrap_count_++;
    last_wrap_time_us_ = time_us_32();

    if (emergency_stop_ || !enabled_) return;

    setSliceComplementary(kSlices[0], manual_du_);
    setSliceComplementary(kSlices[1], manual_dv_);
    setSliceComplementary(kSlices[2], manual_dw_);
}


void PWMDriver::forceAllGpioLow() {
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
    for (uint i = 0; i < kPhaseCount; ++i) {
        gpio_set_function(kPins[i].a, GPIO_FUNC_PWM);
        gpio_set_function(kPins[i].b, GPIO_FUNC_PWM);
    }
}
