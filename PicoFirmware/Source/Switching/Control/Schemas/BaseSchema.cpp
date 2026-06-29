/**
***********************************************************************************
* @file    BaseSchema.cpp
* @date    2026-02-19
* @brief   Implementation of base control scheme logic and bounds checking.
***********************************************************************************
*/

#include "BaseSchema.h"
#include <cmath>

void ControlScheme::ApplyConfig(ControlCommonConfig _Config) {
    Config_ = _Config;
}

void ControlScheme::SetMotorConfig(const MotorConfig& _MotorConfig) {
    MotorConfig_ = _MotorConfig; 
}

bool ControlScheme::IsActiveAtVelocity(float _Velocity_RadPerSec, float _TransitionWindow_RadPerSec) {
    // Use absolute velocity so the scheme works symmetrically in forward and reverse
    float absVelocity = std::abs(_Velocity_RadPerSec);
    
    bool isAboveMin = absVelocity >= (Config_.InfluenceStart_RadPerSec_ - _TransitionWindow_RadPerSec);
    bool isBelowMax = absVelocity <= (Config_.InfluenceEnd_RadPerSec_   + _TransitionWindow_RadPerSec);

    return (isAboveMin && isBelowMax);
}