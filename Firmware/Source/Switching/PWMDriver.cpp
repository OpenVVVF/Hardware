#include "PWMDriver.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "Hardware.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Static instance pointer for ISR
PWMDriver* PWMDriver::instance_ = nullptr;

// C-compatible ISR wrapper
extern "C" void pwm_wrap_isr() {
    if (PWMDriver::instance()) {
        PWMDriver::instance()->isrHandler();
    }
}

// ============================================================================
// FOC Strategy implementation
// ============================================================================

static inline float fast_max3(float a, float b, float c) {
    return fmaxf(a, fmaxf(b, c));
}

static inline float fast_min3(float a, float b, float c) {
    return fminf(a, fminf(b, c));
}

FOCStrategy::FOCStrategy() {
    resetControllers();
    resetCalibrationState();
}

float FOCStrategy::clamp(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

float FOCStrategy::wrap_0_2pi(float a) {
    const float TWO_PI = 2.0f * static_cast<float>(M_PI);
    // fmodf can be a bit slow but acceptable at 2-12kHz on RP2040; optimize later if needed.
    a = fmodf(a, TWO_PI);
    if (a < 0.0f) a += TWO_PI;
    return a;
}

float FOCStrategy::angle_diff(float a, float b) {
    // returns a-b wrapped to [-pi, +pi]
    const float PI = static_cast<float>(M_PI);
    const float TWO_PI = 2.0f * PI;
    float d = a - b;
    while (d > PI) d -= TWO_PI;
    while (d < -PI) d += TWO_PI;
    return d;
}

void FOCStrategy::resetControllers() {
    id_int_ = 0.0f;
    iq_int_ = 0.0f;
    spd_int_ = 0.0f;
    have_prev_ = false;
    prev_t_us_ = 0;
    prev_theta_e_ = 0.0f;
    omega_e_ = 0.0f;
    speed_loop_accum_ = 0.0f;
}

void FOCStrategy::resetCalibrationState() {
    cal_state_ = CalState::IDLE;
    tried_dir_flip_ = false;
    tried_pi_shift_ = false;
    cal_state_start_us_ = 0;
    theta_mech_align_raw_ = 0.0f;
    theta_mech_verify_start_ = 0.0f;
    last_driver_enabled_ = false;
}



void FOCStrategy::computeDuties(float /*theta*/, float /*mod_index*/, uint16_t top,
                               uint16_t& duty_u, uint16_t& duty_v, uint16_t& duty_w) {
    // Safe default (zero vector)
    duty_u = top / 2;
    duty_v = top / 2;
    duty_w = top / 2;

    if (!meas_read_ || !driver_ || driver_->isEmergencyStopped()) {
        resetControllers();
        cal_state_ = CalState::IDLE;
        last_driver_enabled_ = false;
        return;
    }

    const bool enabled = driver_->isEnabled();

    // Read latest measurement snapshot (lock-free)
    FocMeasurement m{};
    if (!meas_read_(&m)) {
        // If we can't get a consistent snapshot, keep zero vector.
        return;
    }

    // If we just transitioned disabled->enabled, start calibration (if enabled)
    if (!last_driver_enabled_ && enabled) {
        resetControllers();
        if (autocal_enabled_ && !calibrated_) {
            cal_state_ = CalState::ALIGN;
            cal_state_start_us_ = m.t_us;
            tried_dir_flip_ = false;
            tried_pi_shift_ = false;
            enc_dir_ = +1;
        } else {
            cal_state_ = CalState::RUN;
        }
    }
    last_driver_enabled_ = enabled;

    // If disabled, keep zero vector and reset control integrators (but keep calibration result)
    if (!enabled) {
        resetControllers();
        cal_state_ = CalState::IDLE;
        return;
    }

    // Basic validity
    if (!(m.v_bus > 1.0f) || !std::isfinite(m.theta_mech_rad)) {
        resetControllers();
        return;
    }

    // Reject stale sensor snapshots (e.g., core0 stalled)
    const uint32_t now_us = time_us_32();
    if ((uint32_t)(now_us - m.t_us) > 5000u) { // >5ms old
        resetControllers();
        return;
    }

    // --- Clarke transform (a=u, b=v, c=w). Assumes i_u + i_v + i_w ≈ 0. ---
    const float i_u = m.i_u;
    const float i_v = m.i_v;
    const float INV_SQRT3 = 0.57735026919f;
    const float i_alpha = i_u;
    const float i_beta  = (i_u + 2.0f * i_v) * INV_SQRT3;

    // dt for ISR-rate current loop
    const float dt_isr = 1.0f / fmaxf(100.0f, driver_->getCarrierFrequency());

    // --- Calibration state machine ---
    const uint32_t elapsed_us = (uint32_t)(m.t_us - cal_state_start_us_);
    const float TWO_PI = 2.0f * static_cast<float>(M_PI);

    float theta_ctrl = 0.0f; // the angle used for Park/Inverse-Park this cycle
    float id_ref = 0.0f;
    float iq_ref = 0.0f;

    if (autocal_enabled_ && !calibrated_) {
        if (cal_state_ == CalState::IDLE) {
            // Shouldn't happen if we started calibration on enable, but handle anyway.
            cal_state_ = CalState::ALIGN;
            cal_state_start_us_ = m.t_us;
            tried_dir_flip_ = false;
            tried_pi_shift_ = false;
            enc_dir_ = +1;
        }

        if (cal_state_ == CalState::ALIGN) {
            // Force a stationary stator field along theta_ctrl=0 using a forced reference frame.
            theta_ctrl = 0.0f;
            id_ref = align_id_a_;
            iq_ref = 0.0f;

            if (elapsed_us >= align_time_ms_ * 1000u) {
                // Capture the encoder mechanical angle once the rotor has settled.
                theta_mech_align_raw_ = m.theta_mech_rad;

                // Choose elec_offset so that theta_e = p*dir*theta_mech + offset == 0 at the locked position.
                elec_offset_rad_ = wrap_0_2pi(-static_cast<float>(pole_pairs_) * static_cast<float>(enc_dir_) * theta_mech_align_raw_);

                // Move to VERIFY
                cal_state_ = CalState::VERIFY;
                cal_state_start_us_ = m.t_us;
                theta_mech_verify_start_ = m.theta_mech_rad;
                resetControllers();
            }
        }

        if (cal_state_ == CalState::VERIFY) {
            theta_ctrl = wrap_0_2pi(static_cast<float>(pole_pairs_) * static_cast<float>(enc_dir_) * m.theta_mech_rad + elec_offset_rad_);
            id_ref = 0.0f;
            iq_ref = clamp(verify_iq_a_, 0.0f, iq_max_);

            if (elapsed_us >= verify_time_ms_ * 1000u) {
                // Use raw encoder motion sign to decide whether we want to flip direction.
                const float delta_mech = angle_diff(m.theta_mech_rad, theta_mech_verify_start_);

                // "Moved enough" threshold (rad). Keep it low; motor is free spinning.
                const float MIN_MOVE = 0.05f; // ~3 degrees mech

                if (delta_mech < -MIN_MOVE && !tried_dir_flip_) {
                    // Motor moved opposite to encoder-positive direction.
                    // Flip our encoder direction mapping so +Iq produces +encoder motion.
                    enc_dir_ = -enc_dir_;
                    tried_dir_flip_ = true;
                    tried_pi_shift_ = false;

                    elec_offset_rad_ = wrap_0_2pi(-static_cast<float>(pole_pairs_) * static_cast<float>(enc_dir_) * theta_mech_align_raw_);
                    cal_state_start_us_ = m.t_us;
                    theta_mech_verify_start_ = m.theta_mech_rad;
                    resetControllers();
                } else if (fabsf(delta_mech) < MIN_MOVE && !tried_pi_shift_) {
                    // Not much motion: try π electrical shift (covers common sign/axis ambiguities).
                    elec_offset_rad_ = wrap_0_2pi(elec_offset_rad_ + static_cast<float>(M_PI));
                    tried_pi_shift_ = true;

                    cal_state_start_us_ = m.t_us;
                    theta_mech_verify_start_ = m.theta_mech_rad;
                    resetControllers();
                } else {
                    // Accept calibration
                    calibrated_ = true;
                    cal_state_ = CalState::RUN;
                    resetControllers();
                }
            }
        }
    }

    // --- RUN mode (normal speed->Iq loop) ---
    if (cal_state_ == CalState::RUN || calibrated_) {
        const float speed_target_hz = driver_->getCurrentFrequency();
        if (fabsf(speed_target_hz) < 0.01f) {
            resetControllers();
            return;
        }

        theta_ctrl = wrap_0_2pi(static_cast<float>(pole_pairs_) * static_cast<float>(enc_dir_) * m.theta_mech_rad + elec_offset_rad_);

        // Timing from measurement timestamp for speed estimate
        float dt_meas = 0.0f;
        if (have_prev_) {
            uint32_t du = (uint32_t)(m.t_us - prev_t_us_);
            dt_meas = (float)du * 1e-6f;
            if (dt_meas < 1e-6f || dt_meas > 0.02f) dt_meas = 0.0f;
        }

        // Electrical speed estimate (rad/s), low-pass filtered
        if (have_prev_ && dt_meas > 0.0f) {
            float dth = angle_diff(theta_ctrl, prev_theta_e_);
            float omega_inst = dth / dt_meas;
            omega_e_ += (omega_inst - omega_e_) * speed_lp_;
            speed_loop_accum_ += dt_meas;
        }
        prev_t_us_ = m.t_us;
        prev_theta_e_ = theta_ctrl;
        have_prev_ = true;

        // Speed loop @ ~1kHz produces iq_ref
        const float omega_ref = speed_target_hz * TWO_PI;
        if (speed_loop_accum_ >= 0.001f) {
            const float dt_spd = speed_loop_accum_;
            speed_loop_accum_ = 0.0f;

            const float e_spd = omega_ref - omega_e_;
            spd_int_ += spd_pi_ki_ * e_spd * dt_spd;
            spd_int_ = clamp(spd_int_, -iq_max_, iq_max_);
            iq_ref = spd_pi_kp_ * e_spd + spd_int_;
            iq_ref = clamp(iq_ref, -iq_max_, iq_max_);
        } else {
            iq_ref = clamp(spd_int_, -iq_max_, iq_max_);
        }

        id_ref = id_ref_;
    } else if (cal_state_ != CalState::ALIGN && cal_state_ != CalState::VERIFY) {
        // If we somehow got here without a valid state, stay safe.
        resetControllers();
        return;
    }

    // --- Park transform using theta_ctrl ---
    const float s = sinf(theta_ctrl);
    const float c = cosf(theta_ctrl);
    const float i_d =  i_alpha * c + i_beta * s;
    const float i_q = -i_alpha * s + i_beta * c;

    // --- Current PI controllers -> v_d, v_q ---
    const float e_id = id_ref - i_d;
    const float e_iq = iq_ref - i_q;

    id_int_ += id_pi_ki_ * e_id * dt_isr;
    iq_int_ += iq_pi_ki_ * e_iq * dt_isr;

    const float v_max = 0.57735026919f * m.v_bus; // Vdc/sqrt(3)

    float v_d = id_pi_kp_ * e_id + id_int_;
    float v_q = iq_pi_kp_ * e_iq + iq_int_;

    // Vector magnitude saturation + simple back-calculation anti-windup
    float v_mag = sqrtf(v_d * v_d + v_q * v_q);
    if (v_mag > v_max && v_mag > 1e-6f) {
        float scale = v_max / v_mag;
        v_d *= scale;
        v_q *= scale;
        id_int_ = v_d - id_pi_kp_ * e_id;
        iq_int_ = v_q - iq_pi_kp_ * e_iq;
    }

    // Inverse Park to alpha-beta voltages
    const float v_alpha = v_d * c - v_q * s;
    const float v_beta  = v_d * s + v_q * c;

    // SVPWM via common-mode injection
    const float SQRT3_BY_2 = 0.86602540378f;
    float v_u = v_alpha;
    float v_v = -0.5f * v_alpha + SQRT3_BY_2 * v_beta;
    float v_w = -0.5f * v_alpha - SQRT3_BY_2 * v_beta;

    float v_max_ph = fast_max3(v_u, v_v, v_w);
    float v_min_ph = fast_min3(v_u, v_v, v_w);
    float v_off = -0.5f * (v_max_ph + v_min_ph);
    v_u += v_off;
    v_v += v_off;
    v_w += v_off;

    const float inv_vdc = 1.0f / m.v_bus;
    float du = 0.5f + v_u * inv_vdc;
    float dv = 0.5f + v_v * inv_vdc;
    float dw = 0.5f + v_w * inv_vdc;

    du = clamp(du, 0.0f, 1.0f);
    dv = clamp(dv, 0.0f, 1.0f);
    dw = clamp(dw, 0.0f, 1.0f);

    duty_u = (uint16_t)(du * (float)top);
    duty_v = (uint16_t)(dv * (float)top);
    duty_w = (uint16_t)(dw * (float)top);
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