/**
***********************************************************************************
* @file    VectorPIController.cpp
* @date    2026-02-19
* @brief   Implementation of the coupled D/Q axis Vector PI controller.
***********************************************************************************
*/

#include "VectorPIController.h"
#include <cmath>

void VectorPiController::Reset() {
    IdInt_ = 0.0f;
    IqInt_ = 0.0f;
}

void VectorPiController::Update(float _IdErr, float _IqErr, float _VdFf, float _VqFf, float _dt_S, float& _VdOut, float& _VqOut) {
    // 1. Proportional terms
    float VdP = _IdErr * Kp_;
    float VqP = _IqErr * Kp_;

    // 2. Pre-limit output (P + current I + Feedforward)
    float VdPre = VdP + IdInt_ + _VdFf;
    float VqPre = VqP + IqInt_ + _VqFf;

    // 3. Dynamic Circular Limit Check
    float Vmag = std::sqrt(VdPre * VdPre + VqPre * VqPre);

    if (Vmag > MaxVoltageLimit_ && Vmag > 1e-6f) {
        float Scale = MaxVoltageLimit_ / Vmag;
        _VdOut = VdPre * Scale;
        _VqOut = VqPre * Scale;
    } else {
        _VdOut = VdPre;
        _VqOut = VqPre;
    }

    // 4. Back-Calculation Anti-Windup & Integration
    float VdExcess = VdPre - _VdOut;
    float VqExcess = VqPre - _VqOut;
    float Ka = (Kp_ > 0.0001f) ? (1.0f / Kp_) : 0.0f;

    IdInt_ += (Ki_ * _IdErr * _dt_S) - (VdExcess * Ka * _dt_S);
    IqInt_ += (Ki_ * _IqErr * _dt_S) - (VqExcess * Ka * _dt_S);
}