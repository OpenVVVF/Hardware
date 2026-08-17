#include "Inverter/Control/VectorPIController.h"
#include "Inverter/Control/Math/FocMath.h"

#include <cmath>

namespace Inverter {

void VectorPIController::Reset() {
    id_int_ = 0.0f;
    iq_int_ = 0.0f;
    vd_filter_state_ = 0.0f;
    vq_filter_state_ = 0.0f;
}

void VectorPIController::Update(float id_err, float iq_err,
                                float vd_ff, float vq_ff,
                                float dt_s,
                                float& vd_out, float& vq_out) {
    // 1. Proportional terms.
    float vd_p = id_err * Kp_;
    float vq_p = iq_err * Kp_;

    // 2. Pre-limit output (P + current I + feedforward).
    float vd_pre = vd_p + id_int_ + vd_ff;
    float vq_pre = vq_p + iq_int_ + vq_ff;

    // 3. Dynamic circular limit.
    float v_mag = std::sqrt(vd_pre * vd_pre + vq_pre * vq_pre);

    float vd_lim, vq_lim;
    if (v_mag > MaxVoltageLimit_ && v_mag > 1e-6f) {
        float scale = MaxVoltageLimit_ / v_mag;
        vd_lim = vd_pre * scale;
        vq_lim = vq_pre * scale;
    } else {
        vd_lim = vd_pre;
        vq_lim = vq_pre;
    }

    // 4. Back-calculation anti-windup.
    float vd_excess = vd_pre - vd_lim;
    float vq_excess = vq_pre - vq_lim;
    float ka = (Kp_ > 0.0001f) ? (1.0f / Kp_) : 0.0f;

    id_int_ += (Ki_ * id_err * dt_s) - (vd_excess * ka * dt_s);
    iq_int_ += (Ki_ * iq_err * dt_s) - (vq_excess * ka * dt_s);

    // 5. Optional output low-pass filter.
    if (EnableOutputFilter_ && FilterCutoffHz_ > 0.0f) {
        float rc = 1.0f / (2.0f * FOC_PI * FilterCutoffHz_);
        float alpha = dt_s / (rc + dt_s);
        if (alpha > 1.0f) alpha = 1.0f;
        if (alpha < 0.0f) alpha = 0.0f;

        vd_filter_state_ += alpha * (vd_lim - vd_filter_state_);
        vq_filter_state_ += alpha * (vq_lim - vq_filter_state_);

        vd_out = vd_filter_state_;
        vq_out = vq_filter_state_;
    } else {
        vd_out = vd_lim;
        vq_out = vq_lim;

        vd_filter_state_ = vd_lim;
        vq_filter_state_ = vq_lim;
    }
}

} // namespace Inverter
