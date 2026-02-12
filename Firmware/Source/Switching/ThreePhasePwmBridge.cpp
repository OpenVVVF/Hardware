// ThreePhasePwmBridge.cpp
#include "ThreePhasePwmBridge.h"

ThreePhasePwmBridge* ThreePhasePwmBridge::irq_owner_ = nullptr;

extern "C" void pwm_wrap_isr() {
    if (ThreePhasePwmBridge::irqOwner()) {
        ThreePhasePwmBridge::irqOwner()->isrHandler();
    }
}

ThreePhasePwmBridge::ThreePhasePwmBridge(const Config& cfg) : config_(cfg) {
    // Map phases to slices and validate pins.
    slice_mask_ = 0;

    for (uint i = 0; i < kNumPhases; i++) {
        const uint ga = config_.phase[i].gpio_a;
        const uint gb = config_.phase[i].gpio_b;

        const uint sa = pwm_gpio_to_slice_num(ga);
        const uint sb = pwm_gpio_to_slice_num(gb);

        // Basic requirement: both pins must be on the same slice and different channels.
        hard_assert(sa == sb);
        hard_assert(pwm_gpio_to_channel(ga) != pwm_gpio_to_channel(gb));

        slice_[i] = sa;
        slice_mask_ |= (1u << sa);
    }

    hard_assert(config_.irq_phase < kNumPhases);
    irq_slice_ = slice_[config_.irq_phase];

    // Single owner of PWM_IRQ_WRAP in this simple design.
    // If you need multiple owners, you’d need a small dispatcher.
    irq_owner_ = this;
}

void ThreePhasePwmBridge::init(float initial_carrier_hz) {
    setCarrierFrequency(initial_carrier_hz);
    forceAllGpioLow();
}

void ThreePhasePwmBridge::computeTopDivForCarrier_(float carrier_hz, uint16_t& top_out, float& div_out) const {
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    const float factor = config_.center_aligned ? 2.0f : 1.0f;

    float ideal_top = (static_cast<float>(sys_hz) / (factor * carrier_hz)) - 1.0f;

    float div = 1.0f;
    uint16_t top = 0;

    if (ideal_top > 65535.0f) {
        // quantize divider to 0.1 steps like your original code
        div = ceilf((ideal_top / 65535.0f) * 10.0f) / 10.0f;
        if (div > 255.0f) div = 255.0f;
        top = static_cast<uint16_t>((static_cast<float>(sys_hz) / (factor * carrier_hz * div)) - 1.0f);
    } else {
        div = 1.0f;
        top = static_cast<uint16_t>(ideal_top);
    }

    top_out = top;
    div_out = div;
}

void ThreePhasePwmBridge::setCarrierFrequency(float hz) {
    if (hz < config_.carrier_min_hz) hz = config_.carrier_min_hz;
    if (hz > config_.carrier_max_hz) hz = config_.carrier_max_hz;

    carrier_hz_ = hz;

    if (enabled_) {
        updateHardwareClock_(hz);
    } else {
        computeTopDivForCarrier_(hz, pwm_top_, clk_div_);
    }
}

void ThreePhasePwmBridge::updateHardwareClock_(float carrier_hz) {
    uint16_t new_top;
    float new_div;
    computeTopDivForCarrier_(carrier_hz, new_top, new_div);

    const uint32_t irq_state = save_and_disable_interrupts();

    for (uint i = 0; i < kNumPhases; i++) {
        const uint s = slice_[i];
        pwm_set_clkdiv(s, new_div);
        pwm_set_wrap(s, new_top);
    }

    pwm_top_ = new_top;
    clk_div_ = new_div;

    restore_interrupts(irq_state);
}

void ThreePhasePwmBridge::enable() {
    if (emergency_stop_) return;
    if (enabled_) return;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, clk_div_);
    pwm_config_set_wrap(&cfg, pwm_top_);
    pwm_config_set_phase_correct(&cfg, false); // sync in edge-aligned first

    // Init all slices/pins
    for (uint i = 0; i < kNumPhases; i++) {
        const uint ga = config_.phase[i].gpio_a;
        const uint gb = config_.phase[i].gpio_b;
        const uint s  = slice_[i];

        gpio_set_function(ga, GPIO_FUNC_PWM);
        gpio_set_function(gb, GPIO_FUNC_PWM);

        pwm_init(s, &cfg, false);
        pwm_set_output_polarity(s, config_.invert_chan_a, config_.invert_chan_b);

        // 50% neutral
        pwm_set_chan_level(s, PWM_CHAN_A, pwm_top_ / 2);
        pwm_set_chan_level(s, PWM_CHAN_B, pwm_top_ / 2);
    }

    // Align counters
    for (uint i = 0; i < kNumPhases; i++) {
        pwm_set_counter(slice_[i], 0);
    }

    // Sync start: enable briefly then disable, preserving other slices
    const uint32_t en0 = pwm_hw->en;
    pwm_set_mask_enabled(en0 | slice_mask_);
    sleep_us(50);
    pwm_set_mask_enabled(en0);

    // Switch to center-aligned if requested
    if (config_.center_aligned) {
        for (uint i = 0; i < kNumPhases; i++) {
            pwm_set_phase_correct(slice_[i], true);
        }
    }

    // Reset and start
    for (uint i = 0; i < kNumPhases; i++) {
        pwm_set_counter(slice_[i], 0);
    }
    pwm_set_mask_enabled(en0 | slice_mask_);

    enabled_ = true;

    // Configure wrap IRQ if user wants it
    configureWrapIRQ_(wrap_irq_enabled_);
}

void ThreePhasePwmBridge::disable() {
    enabled_ = false;

    // Disable wrap irq for our selected slice
    configureWrapIRQ_(false);

    // Disable only our slices, preserve other slices
    const uint32_t en = pwm_hw->en;
    pwm_set_mask_enabled(en & ~slice_mask_);

    forceAllGpioLow();
}

void ThreePhasePwmBridge::emergencyStop() {
    emergency_stop_ = true;
    disable();
}

void ThreePhasePwmBridge::clearEmergency(bool reenable) {
    emergency_stop_ = false;
    restorePwmPins();
    sleep_us(10);
    if (reenable) enable();
}

void ThreePhasePwmBridge::forceAllGpioLow() {
    for (uint i = 0; i < kNumPhases; i++) {
        auto force_low = [&](uint gpio) {
            gpio_set_function(gpio, GPIO_FUNC_SIO);
            gpio_set_dir(gpio, GPIO_OUT);
            gpio_put(gpio, 0);
        };
        force_low(config_.phase[i].gpio_a);
        force_low(config_.phase[i].gpio_b);
    }
}

void ThreePhasePwmBridge::restorePwmPins() {
    for (uint i = 0; i < kNumPhases; i++) {
        gpio_set_function(config_.phase[i].gpio_a, GPIO_FUNC_PWM);
        gpio_set_function(config_.phase[i].gpio_b, GPIO_FUNC_PWM);
    }
}

uint16_t ThreePhasePwmBridge::clampDuty_(uint16_t x) const {
    const uint16_t lo = static_cast<uint16_t>(pwm_top_ * (config_.min_duty_percent / 100.0f));
    const uint16_t hi = static_cast<uint16_t>(pwm_top_ * (config_.max_duty_percent / 100.0f));
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

void ThreePhasePwmBridge::applyDutyToSlice_(uint phase_index, uint16_t level_ticks) {
    const uint s = slice_[phase_index];
    pwm_set_chan_level(s, PWM_CHAN_A, level_ticks);
    pwm_set_chan_level(s, PWM_CHAN_B, level_ticks);
}

void ThreePhasePwmBridge::setDutyTicks(uint16_t du, uint16_t dv, uint16_t dw) {
    if (!enabled_ || emergency_stop_) return;

    du = clampDuty_(du);
    dv = clampDuty_(dv);
    dw = clampDuty_(dw);

    applyDutyToSlice_(0, du);
    applyDutyToSlice_(1, dv);
    applyDutyToSlice_(2, dw);
}

void ThreePhasePwmBridge::setPhaseVoltagesPU(float vu, float vv, float vw) {
    if (!enabled_ || emergency_stop_) return;

    auto v_to_ticks = [&](float v) -> uint16_t {
        // [-1..1] -> [0..1]
        float x = 0.5f * (v + 1.0f);
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        return static_cast<uint16_t>(x * static_cast<float>(pwm_top_));
    };

    setDutyTicks(v_to_ticks(vu), v_to_ticks(vv), v_to_ticks(vw));
}

void ThreePhasePwmBridge::setPhaseVoltagesVolts(float vu, float vv, float vw, float vbus_volts) {
    if (vbus_volts <= 0.01f) return;
    const float half = 0.5f * vbus_volts;
    setPhaseVoltagesPU(vu / half, vv / half, vw / half);
}

void ThreePhasePwmBridge::setWrapCallback(WrapCallback cb, void* user) {
    wrap_cb_ = cb;
    wrap_user_ = user;
}

void ThreePhasePwmBridge::enableWrapIRQ(bool enable) {
    wrap_irq_enabled_ = enable;
    if (enabled_) configureWrapIRQ_(enable);
}

void ThreePhasePwmBridge::configureWrapIRQ_(bool enable) {
    // Always disable first (idempotent)
    pwm_set_irq_enabled(irq_slice_, false);

    if (!enable || !enabled_) {
        // Leave global IRQ enabled state alone; we just stop generating slice IRQs.
        return;
    }

    pwm_clear_irq(irq_slice_);
    pwm_set_irq_enabled(irq_slice_, true);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void ThreePhasePwmBridge::isrHandler() {
    // Clear the selected slice IRQ
    pwm_clear_irq(irq_slice_);

    if (!enabled_ || emergency_stop_) return;
    if (wrap_cb_) wrap_cb_(wrap_user_);
}
