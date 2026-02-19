/**
***********************************************************************************
* @file    PIDController.cpp
* @date    2026-02-19
* @brief   Implementation of the generic PID controller.
***********************************************************************************
*/

#include "PIDController.h"

void PidController::Reset() {
    Integral_ = 0.0f;
    PrevError_ = 0.0f;
}

float PidController::Update(float _Error, float _Feedforward, float _dt_S) {
    // 1. Proportional and Derivative Terms
    float PTerm = Kp_ * _Error;
    float DTerm = 0.0f;
    
    if (_dt_S > 0.0f) {
        DTerm = Kd_ * (_Error - PrevError_) / _dt_S;
    }
    PrevError_ = _Error;

    // 2. Pre-limit Output
    float PreLimitOut = PTerm + Integral_ + DTerm + _Feedforward;

    // 3. Saturation (Clamping)
    float Out = PreLimitOut;
    if (Out > MaxOutput_) {
        Out = MaxOutput_;
    } else if (Out < MinOutput_) {
        Out = MinOutput_;
    }

    // 4. Integration with Back-Calculation Anti-Windup
    float Excess = PreLimitOut - Out;
    float Ka = (AntiWindupGain_ > 0.0001f) ? AntiWindupGain_ : ((Kp_ > 0.0001f) ? (1.0f / Kp_) : 0.0f);
    
    Integral_ += (Ki_ * _Error * _dt_S) - (Excess * Ka * _dt_S);

    return Out;
}