/**
***********************************************************************************
* @file    VectorPIController.h
* @date    2026-02-19
* @brief   Coupled Vector PI Controller for D/Q axes with Dynamic Circular Anti-Windup.
***********************************************************************************
*/

#pragma once

class VectorPiController {
public:
    float Kp_ = 0.0f;
    float Ki_ = 0.0f;
    float MaxVoltageLimit_ = 0.0f;  ///< Maximum allowed vector magnitude

    VectorPiController() = default;

    void Reset();

    /**
    * @brief Computes decoupled D/Q voltages while respecting a shared circular hardware limit.
    */
    void Update(float _IdErr, float _IqErr, float _VdFf, float _VqFf, float _dt_S, float& _VdOut, float& _VqOut);

private:
    float IdInt_ = 0.0f;
    float IqInt_ = 0.0f;
};