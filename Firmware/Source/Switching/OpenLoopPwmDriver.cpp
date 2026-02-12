// OpenLoopPwmDriver.cpp
#include "OpenLoopPwmDriver.h"

void SPWMStrategy::computeDuties(float theta, float mod_index, uint16_t top,
                                uint16_t& duty_u, uint16_t& duty_v, uint16_t& duty_w) {
    float su = sinf(theta);
    float sv = sinf(theta - 2.0f * static_cast<float>(M_PI) / 3.0f);
    float sw = sinf(theta + 2.0f * static_cast<float>(M_PI) / 3.0f);

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

OpenLoopPwmDriver::OpenLoopPwmDriver(const Config& cfg)
    : config_(cfg), bridge_(cfg.bridge) {
    auto_modulation_ = cfg.auto_modulation;
}

void OpenLoopPwmDriver::init(float initial_carrier_hz) {
    bridge_.init(initial_carrier_hz);

    // Open-loop layer wants PWM-wrap pacing
    bridge_.setWrapCallback(&OpenLoopPwmDriver::wrapThunk_, this);
    bridge_.enableWrapIRQ(true);
}

void OpenLoopPwmDriver::setStrategy(ModulationStrategy* strategy) {
    strategy_ = strategy;
}

void OpenLoopPwmDriver::setCarrierFrequency(float hz) {
    bridge_.setCarrierFrequency(hz);
    updatePhaseStep_();
}

void OpenLoopPwmDriver::setTargetFrequency(float hz, float ramp_rate_hz_per_sec) {
    target_freq_ = hz;
    ramp_rate_ = ramp_rate_hz_per_sec;
}

void OpenLoopPwmDriver::setFrequencyImmediate(float hz) {
    target_freq_ = hz;
    current_freq_ = hz;
    updatePhaseStep_();
}

void OpenLoopPwmDriver::setModulationIndex(float mi) {
    mod_index_ = mi;
    auto_modulation_ = false;
}

void OpenLoopPwmDriver::setAutoModulation(bool enable) {
    auto_modulation_ = enable;
}

void OpenLoopPwmDriver::setSynchronousMode(bool enable, uint16_t pulses_per_cycle) {
    sync_mode_ = enable;
    pulses_per_cycle_ = pulses_per_cycle;
    updatePhaseStep_();
}

void OpenLoopPwmDriver::updatePhaseStep_() {
    const float carrier = bridge_.getCarrierFrequency();

    if (sync_mode_ && std::fabs(current_freq_) > 0.01f && pulses_per_cycle_ > 0) {
        dtheta_ = 2.0f * static_cast<float>(M_PI) / static_cast<float>(pulses_per_cycle_);
    } else {
        if (carrier > 0.0f) {
            dtheta_ = 2.0f * static_cast<float>(M_PI) * current_freq_ / carrier;
        } else {
            dtheta_ = 0.0f;
        }
    }
}

void OpenLoopPwmDriver::enable() {
    if (bridge_.isEmergencyStopped()) return;
    if (enabled_) return;

    bridge_.enable();
    enabled_ = true;

    theta_ = 0.0f;
    updatePhaseStep_();
}

void OpenLoopPwmDriver::disable() {
    enabled_ = false;
    bridge_.disable();

    target_freq_ = 0.0f;
    current_freq_ = 0.0f;
    theta_ = 0.0f;
}

void OpenLoopPwmDriver::emergencyStop() {
    enabled_ = false;
    bridge_.emergencyStop();

    target_freq_ = 0.0f;
    current_freq_ = 0.0f;
    theta_ = 0.0f;
}

void OpenLoopPwmDriver::clearEmergency() {
    bridge_.clearEmergency(true);
    enabled_ = bridge_.isEnabled();
    theta_ = 0.0f;
    updatePhaseStep_();
}

void OpenLoopPwmDriver::update(float dt_seconds) {
    if (bridge_.isEmergencyStopped()) return;

    // Frequency ramping
    float err = target_freq_ - current_freq_;
    if (std::fabs(err) > 0.01f) {
        float step = ramp_rate_ * dt_seconds;
        if (err > 0) current_freq_ += (err > step ? step : err);
        else         current_freq_ -= (-err > step ? step : -err);

        updatePhaseStep_();
    } else {
        current_freq_ = target_freq_;
    }

    // Auto-disable when ramped to zero
    if (enabled_ && target_freq_ == 0.0f && std::fabs(current_freq_) < 0.01f) {
        disable();
        return;
    }

    // Auto modulation curve (matches your original)
    if (auto_modulation_) {
        float abs_freq = std::fabs(current_freq_);
        if (abs_freq <= 0.0f) {
            mod_index_ = config_.auto_mi_min;
        } else if (abs_freq >= config_.auto_mi_full_freq_hz) {
            mod_index_ = config_.auto_mi_max;
        } else {
            mod_index_ = config_.auto_mi_min +
                (abs_freq / config_.auto_mi_full_freq_hz) * (config_.auto_mi_max - config_.auto_mi_min);
        }
    }

    // Enable PWM if we have a target but were disabled
    if (!enabled_ && std::fabs(target_freq_) > 0.01f && !bridge_.isEmergencyStopped()) {
        enable();
    }
}

void OpenLoopPwmDriver::wrapThunk_(void* user) {
    static_cast<OpenLoopPwmDriver*>(user)->onPwmWrap_();
}

void OpenLoopPwmDriver::onPwmWrap_() {
    if (!enabled_ || bridge_.isEmergencyStopped() || !strategy_) return;

    // Compute duties and apply via bridge (bridge enforces safety clamps)
    uint16_t du, dv, dw;
    strategy_->computeDuties(theta_, mod_index_, bridge_.getTop(), du, dv, dw);
    bridge_.setDutyTicks(du, dv, dw);

    // Advance electrical angle
    theta_ += dtheta_;
    const float two_pi = 2.0f * static_cast<float>(M_PI);
    if (theta_ >= two_pi) theta_ -= two_pi;
    if (theta_ < 0.0f)    theta_ += two_pi;
}
