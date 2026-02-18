/**
 ***********************************************************************************
 * @file    VectorPIController.h
 * @date    2026-02-17
 * @brief   PI Controller for Q and D axis current for FOC.
 ***********************************************************************************
 */

#pragma once

/**
 * @brief Coupled Vector PI Controller for D/Q axes with dynamic anti-windup
 */
class VectorPIController {
   public:
    float Kp = 0.0f;
    float Ki = 0.0f;
    float MaxVoltageLimit = 0.0f;  // Limit for sqrt(Vd^2 + Vq^2)

    float Id_int = 0.0f;
    float Iq_int = 0.0f;

    void Reset();

    void Update(float Id_err, float Iq_err, float Vd_ff, float Vq_ff, float dt, float& Vd_out, float& Vq_out);
};