/**
***********************************************************************************
* @file    VectorPIController.cpp
* @date    2026-02-21
* @brief   Implementation of the coupled D/Q axis Vector PI controller.
***********************************************************************************
*/

#include "VectorPIController.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

void VectorPiController::Reset() {
    IdInt_ = 0.0f;
    IqInt_ = 0.0f;
    
    // Reset filter states
    VdFilterState_ = 0.0f;
    VqFilterState_ = 0.0f;
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
    
    float VdLim, VqLim;
    if (Vmag > MaxVoltageLimit_ && Vmag > 1e-6f) {
        float Scale = MaxVoltageLimit_ / Vmag;
        VdLim = VdPre * Scale;
        VqLim = VqPre * Scale;
    } else {
        VdLim = VdPre;
        VqLim = VqPre;
    }

    // 4. Back-Calculation Anti-Windup & Integration
    // Computed using the UNFILTERED limited output to prevent filter lag from causing windup
    float VdExcess = VdPre - VdLim;
    float VqExcess = VqPre - VqLim;
    float Ka = (Kp_ > 0.0001f) ? (1.0f / Kp_) : 0.0f;

    IdInt_ += (Ki_ * _IdErr * _dt_S) - (VdExcess * Ka * _dt_S);
    IqInt_ += (Ki_ * _IqErr * _dt_S) - (VqExcess * Ka * _dt_S);

    // 5. Optional Output Low-Pass Filter
    if (EnableOutputFilter_ && FilterCutoffHz_ > 0.0f) {
        // Calculate filter coefficient dynamically to support variable dt
        float RC = 1.0f / (2.0f * M_PI * FilterCutoffHz_);
        float alpha = _dt_S / (RC + _dt_S);
        
        // Clamp alpha for stability bounds
        if (alpha > 1.0f) alpha = 1.0f;
        if (alpha < 0.0f) alpha = 0.0f;

        // Apply first-order IIR filter
        VdFilterState_ += alpha * (VdLim - VdFilterState_);
        VqFilterState_ += alpha * (VqLim - VqFilterState_);

        _VdOut = VdFilterState_;
        _VqOut = VqFilterState_;
    } else {
        _VdOut = VdLim;
        _VqOut = VqLim;
        
        // Keep states updated so there's no transient bump if the filter is turned on mid-operation
        VdFilterState_ = VdLim;
        VqFilterState_ = VqLim;
    }
}