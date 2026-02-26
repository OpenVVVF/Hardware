// ========================= PWMDriver.cpp =========================
#include "PWMDriver.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "Hardware.h"
#include <cmath>
#include <algorithm>

PWMDriver* PWMDriver::instance_ = nullptr;

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

    uint16_t cu = (uint16_t)lroundf(du * (float)pwm_top_);
    uint16_t cv = (uint16_t)lroundf(dv * (float)pwm_top_);
    uint16_t cw = (uint16_t)lroundf(dw * (float)pwm_top_);

    cu = clampDuty(cu, min_count_, max_count_);
    cv = clampDuty(cv, min_count_, max_count_);
    cw = clampDuty(cw, min_count_, max_count_);

    irq_set_enabled(PWM_IRQ_WRAP, false);
    manual_du_ = cu;
    manual_dv_ = cv;
    manual_dw_ = cw;
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void PWMDriver::SetNeutralDutycycle() {
    setDutyCycles(0.5, 0.5, 0.5);
}

void PWMDriver::recalcDutyLimits() {
    uint32_t lo = (static_cast<uint32_t>(pwm_top_) * static_cast<uint32_t>(config_.min_duty_percent)) / 100u;
    uint32_t hi = (static_cast<uint32_t>(pwm_top_) * static_cast<uint32_t>(config_.max_duty_percent)) / 100u;

    if (lo > pwm_top_) lo = pwm_top_;
    if (hi > pwm_top_) hi = pwm_top_;
    if (hi < lo) hi = lo;

    min_count_ = static_cast<uint16_t>(lo);
    max_count_ = static_cast<uint16_t>(hi);
}

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
    const double denom = 2.0 * (double)min_carrier_hz * 65536.0;
    double required = (double)sys_hz / denom;

    if (required < 1.0) required = 1.0;
    if (required > 255.0) required = 255.0;

    double q = ceil(required * 16.0) / 16.0;
    if (q > 255.0) q = 255.0;

    fixed_clk_div_ = (float)q;
    clk_div_ = fixed_clk_div_; 
}

uint16_t PWMDriver::computeTopFromCarrier(float carrier_hz) const {
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    double top = ((double)sys_hz / (2.0 * (double)carrier_hz * (double)fixed_clk_div_)) - 1.0;
    
    if (top < 0.0) top = 0.0;
    if (top > 65535.0) top = 65535.0;
    return (uint16_t)llround(top);
}

void PWMDriver::enable() {
    if (emergency_stop_) return;
    sys_hz = clock_get_hz(clk_sys);
    
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, clk_div_);
    pwm_config_set_wrap(&cfg, pwm_top_);
    pwm_config_set_phase_correct(&cfg, false);

    for (uint i = 0; i < kPhaseCount; ++i) {
        uint gpio_a = kPins[i].a;
        uint gpio_b = kPins[i].b;
        uint slice = pwm_gpio_to_slice_num(gpio_a);

        gpio_set_function(gpio_a, GPIO_FUNC_PWM);
        gpio_set_function(gpio_b, GPIO_FUNC_PWM);

        pwm_init(slice, &cfg, false);
        pwm_set_output_polarity(slice, false, true);
        pwm_set_both_levels(slice, pwm_top_ / 2, pwm_top_ / 2);
    }

    for (uint i = 0; i < kPhaseCount; ++i) {
        pwm_set_counter(kSlices[i], 0);
    }

    pwm_set_mask_enabled((1u << kSlices[0]) | (1u << kSlices[1]) | (1u << kSlices[2]));
    sleep_us(50);
    pwm_set_mask_enabled(0);

    for (uint i = 0; i < kPhaseCount; ++i) {
        pwm_set_phase_correct(kSlices[i], true);
    }

    for (uint i = 0; i < kPhaseCount; ++i) {
        pwm_set_counter(kSlices[i], 0);
    }
    pwm_set_mask_enabled((1u << kSlices[0]) | (1u << kSlices[1]) | (1u << kSlices[2]));

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

    // TRIGGER ADC DMA IMMEDIATELY
    if (on_pwm_wrap_cb_) {
        on_pwm_wrap_cb_();
    }

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

void PWMDriver::SetHardwareCommand(HardwareCommand _Cmd) {
    float hz = _Cmd.SwitchingFrequency_Hz;
    float du = _Cmd.DutyPhU_unitless;
    float dv = _Cmd.DutyPhV_unitless;
    float dw = _Cmd.DutyPhW_unitless;
    bool hz_changed = false;
    uint16_t active_top = pwm_top_;
    uint16_t active_min = min_count_;
    uint16_t active_max = max_count_;

    if (std::abs(hz - carrier_hz_) > 0.1f) {
        hz_changed = true;
        if (hz < Hardware::Limits::Switching::MIN_HZ) hz = Hardware::Limits::Switching::MIN_HZ;
        if (hz > Hardware::Limits::Switching::MAX_HZ) hz = Hardware::Limits::Switching::MAX_HZ;

        float top_f = ((float)sys_hz / (2.0f * hz * fixed_clk_div_)) - 1.0f;
        
        if (top_f < 0.0f) top_f = 0.0f;
        if (top_f > 65535.0f) top_f = 65535.0f;
        active_top = (uint16_t)(top_f + 0.5f);

        uint32_t lo = (static_cast<uint32_t>(active_top) * config_.min_duty_percent) / 100u;
        uint32_t hi = (static_cast<uint32_t>(active_top) * config_.max_duty_percent) / 100u;
        
        if (lo > active_top) lo = active_top;
        if (hi > active_top) hi = active_top;
        if (hi < lo) hi = lo;

        active_min = static_cast<uint16_t>(lo);
        active_max = static_cast<uint16_t>(hi);
    }

    if (du < 0.0f) du = 0.0f; else if (du > 1.0f) du = 1.0f;
    if (dv < 0.0f) dv = 0.0f; else if (dv > 1.0f) dv = 1.0f;
    if (dw < 0.0f) dw = 0.0f; else if (dw > 1.0f) dw = 1.0f;

    float f_top = (float)active_top;
    uint16_t cu = (uint16_t)((du * f_top) + 0.5f);
    uint16_t cv = (uint16_t)((dv * f_top) + 0.5f);
    uint16_t cw = (uint16_t)((dw * f_top) + 0.5f);

    if (cu < active_min) cu = active_min; else if (cu > active_max) cu = active_max;
    if (cv < active_min) cv = active_min; else if (cv > active_max) cv = active_max;
    if (cw < active_min) cw = active_min; else if (cw > active_max) cw = active_max;

    irq_set_enabled(PWM_IRQ_WRAP, false);

    if (hz_changed && enabled_) {
        pwm_set_wrap(kSlices[0], active_top);
        pwm_set_wrap(kSlices[1], active_top);
        pwm_set_wrap(kSlices[2], active_top);
    }

    manual_du_ = cu;
    manual_dv_ = cv;
    manual_dw_ = cw;

    irq_set_enabled(PWM_IRQ_WRAP, true);

    if (hz_changed) {
        carrier_hz_ = hz;
        pwm_top_ = active_top;
        min_count_ = active_min;
        max_count_ = active_max;
    }
}