/**
 ***********************************************************************************
 * @file    VectorPIController.cpp
 * @date    2026-02-17
 * @brief   PI Controller for Q and D axis current for FOC.
 ***********************************************************************************
 */

#include "VectorPIController.h"

#include <cmath>

void VectorPIController::Reset() {
    Id_int = 0.0f;
    Iq_int = 0.0f;
}

void VectorPIController::Update(float Id_err, float Iq_err, float Vd_ff, float Vq_ff, float dt, float& Vd_out, float& Vq_out) {
    // 1. Proportional term
    float Vd_p = Id_err * Kp;
    float Vq_p = Iq_err * Kp;

    // 2. Pre-limit output (P + current I + Feedforward)
    // Feedforward is included here so it is subject to the hardware voltage limits!
    float Vd_pre = Vd_p + Id_int + Vd_ff;
    float Vq_pre = Vq_p + Iq_int + Vq_ff;

    // 3. Calculate total voltage vector magnitude
    float v_mag = sqrtf(Vd_pre * Vd_pre + Vq_pre * Vq_pre);

    // 4. Dynamic Circle Limit & Anti-Windup
    if (v_mag > MaxVoltageLimit && v_mag > 1e-6f) {
        // Scale outputs to fit exactly on the limit circle
        float scale = MaxVoltageLimit / v_mag;
        Vd_out = Vd_pre * scale;
        Vq_out = Vq_pre * scale;

        // Leaky Integrator: slowly bleed off windup while saturated
        Id_int *= 0.99f;
        Iq_int *= 0.99f;
    } else {
        // Voltage is within limits, output normally
        Vd_out = Vd_pre;
        Vq_out = Vq_pre;

        // Standard Integration step
        Id_int += Ki * Id_err * dt;
        Iq_int += Ki * Iq_err * dt;
    }
}