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

// ============================================================================
// SPWMLutStrategy
// ============================================================================
SPWMStrategy::SPWMStrategy() {
    // Fill LUT ONCE (outside ISR). This uses sinf but only at init.
    // LUT maps full turn [0..2π) across LUT_SIZE samples.
    for (uint32_t i = 0; i < LUT_SIZE; ++i) {
        float theta = (6.2831853071795864769f * static_cast<float>(i)) / static_cast<float>(LUT_SIZE);
        float s = sinf(theta);
        int32_t v = static_cast<int32_t>(lroundf(s * 32767.0f));
        if (v > 32767) v = 32767;
        if (v < -32767) v = -32767;
        sin_lut_[i] = static_cast<int16_t>(v);
    }
}

inline uint16_t SPWMStrategy::dutyFromPhase(uint32_t phase_q32,
                                               int16_t mod_q15,
                                               uint16_t top,
                                               const int16_t* lut) {
    // index from top LUT_BITS bits
    uint32_t idx = phase_q32 >> INDEX_SHIFT;
    int32_t s = lut[idx]; // [-32767..32767]

    // apply modulation: (s * mod_q15) >> 15 yields [-32767..32767] approx
    int32_t v = (s * static_cast<int32_t>(mod_q15)) >> 15;

    // map [-32767..32767] -> [0..top]
    // (v + 32767) * top / 65534
    uint32_t x = static_cast<uint32_t>(v + 32767) * static_cast<uint32_t>(top);
    return static_cast<uint16_t>(x / 65534u);
}

void SPWMStrategy::computeDuties(uint32_t phase_q32,
                                   int16_t mod_q15,
                                   uint16_t top,
                                   uint16_t& duty_u,
                                   uint16_t& duty_v,
                                   uint16_t& duty_w) {
    // 3-phase: U = θ, V = θ-120°, W = θ+120°
    uint32_t p_u = phase_q32;
    uint32_t p_v = phase_q32 - PHASE_120;
    uint32_t p_w = phase_q32 + PHASE_120;

    duty_u = dutyFromPhase(p_u, mod_q15, top, sin_lut_);
    duty_v = dutyFromPhase(p_v, mod_q15, top, sin_lut_);
    duty_w = dutyFromPhase(p_w, mod_q15, top, sin_lut_);
}
static inline float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

void PWMDriver::setDutyCycles(float du, float dv, float dw) {
    du = clamp01(du);
    dv = clamp01(dv);
    dw = clamp01(dw);

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
    manual_duty_mode_ = true;
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void PWMDriver::clearManualDuties() {
    irq_set_enabled(PWM_IRQ_WRAP, false);
    manual_duty_mode_ = false;
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

int16_t PWMDriver::floatToQ15Clamp01(float x) {
    if (x < 0.0f) x = 0.0f;
    if (x > 0.999969f) x = 0.999969f; // (32767/32768)
    int32_t q = static_cast<int32_t>(lroundf(x * 32768.0f));
    if (q > 32767) q = 32767;
    if (q < 0) q = 0;
    return static_cast<int16_t>(q);
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

void PWMDriver::setStrategy(ModulationStrategy* strategy) {
    strategy_ = strategy;
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

        pwm_top_ = new_top;
        recalcDutyLimits();
        updatePhaseStep();
    } else {
        pwm_top_ = new_top;
        recalcDutyLimits();
        updatePhaseStep();
    }
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
    auto_modulation_ = false;

    // Keep float for API/debug, but store fixed-point for ISR.
    mod_index_ = mi;
    if (mod_index_ < 0.0f) mod_index_ = 0.0f;
    if (mod_index_ > 0.999f) mod_index_ = 0.999f;

    mod_q15_ = floatToQ15Clamp01(mod_index_);
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
    // phase_step_q32 = (electrical_freq / carrier_hz) * 2^32 turns-per-tick
    // If synchronous mode: exactly 1/pulses_per_cycle turns per PWM tick.
    if (sync_mode_ && std::fabs(current_freq_) > 0.01f && pulses_per_cycle_ > 0) {
        // one electrical cycle per pulses_per_cycle PWM ticks
        phase_step_q32_ = static_cast<uint32_t>(
            (static_cast<uint64_t>(1) << 32) / static_cast<uint32_t>(pulses_per_cycle_)
        );
        if (current_freq_ < 0.0f) {
            phase_step_q32_ = 0u - phase_step_q32_; // reverse
        }
        return;
    }

    if (carrier_hz_ <= 0.0f) {
        phase_step_q32_ = 0;
        return;
    }

    // Do this outside ISR, double is fine here (not in IRQ).
    double turns_per_tick = static_cast<double>(current_freq_) / static_cast<double>(carrier_hz_);
    double step = turns_per_tick * 4294967296.0; // 2^32

    // Clamp to uint32 range via int64.
    int64_t s = static_cast<int64_t>(llround(step));
    phase_step_q32_ = static_cast<uint32_t>(s);
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
    phase_q32_ = 0;
    updatePhaseStep();
}

void PWMDriver::disable() {
    enabled_ = false;
    pwm_set_mask_enabled(0);
    forceAllGpioLow();
    target_freq_ = 0.0f;
    current_freq_ = 0.0f;
    phase_q32_ = 0;
}

void PWMDriver::emergencyStop() {
    emergency_stop_ = true;
    enabled_ = false;
    pwm_set_mask_enabled(0);
    forceAllGpioLow();
    target_freq_ = 0.0f;
    current_freq_ = 0.0f;
    phase_q32_ = 0;
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
        if (err > 0.0f) {
            current_freq_ += (err > step ? step : err);
        } else {
            current_freq_ -= (-err > step ? step : -err);
        }
        updatePhaseStep();
    } else {
        current_freq_ = target_freq_;
    }

    // Auto-disable when ramped to zero
    if (enabled_ && target_freq_ == 0.0f && std::fabs(current_freq_) < 0.01f) {
        disable();
        return;
    }

    // Auto-modulation curve (kept from your original behavior)
    if (auto_modulation_) {
        float abs_freq = std::fabs(current_freq_);
        if (abs_freq <= 0.0f) {
            mod_index_ = 0.04f;
        } else if (abs_freq >= 120.0f) {
            mod_index_ = 0.99f;
        } else {
            mod_index_ = 0.04f + (abs_freq / 120.0f) * (0.99f - 0.04f);
        }
        mod_q15_ = floatToQ15Clamp01(mod_index_);
    }

    // Enable PWM if we have a target but were disabled
    if (!enabled_ && std::fabs(target_freq_) > 0.01f && !emergency_stop_) {
        enable();
    }
}

void PWMDriver::isrHandler() {
    pwm_clear_irq(kSlices[0]);
    pwm_wrap_count_++;
    last_wrap_time_us_ = time_us_32();

    if (emergency_stop_ || !enabled_) return;

    uint16_t du, dv, dw;

    if (manual_duty_mode_) {
        du = manual_du_;
        dv = manual_dv_;
        dw = manual_dw_;
    } else {
        if (!strategy_) return;

        strategy_->computeDuties(phase_q32_, mod_q15_, pwm_top_, du, dv, dw);

        du = clampDuty(du, min_count_, max_count_);
        dv = clampDuty(dv, min_count_, max_count_);
        dw = clampDuty(dw, min_count_, max_count_);

        phase_q32_ += phase_step_q32_;
    }

    setSliceComplementary(kSlices[0], du);
    setSliceComplementary(kSlices[1], dv);
    setSliceComplementary(kSlices[2], dw);
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
