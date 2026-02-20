/**
***********************************************************************************
* @file    CurrentController.cpp
* @date    2026-02-19
* @brief   Implementation of the outer-loop Current/Torque controller.
***********************************************************************************
*/

#include "CurrentController.h"
#include <algorithm>
#include <cmath>

void CurrentController::Reset() {
    _ActiveIqCmd_A = 0.0f;
    _ActiveIdCmd_A = 0.0f;
}

DriveCommand CurrentController::Update(const SensorData& _Sensors, 
                                       const BaseMotionSetpoint& _Target, 
                                       float _dt_S) {
                                        
    // 1. Safely cast the empty base struct to our specific setpoint
    const CurrentSetpoint& target = static_cast<const CurrentSetpoint&>(_Target);

    // 2. Calculate the maximum allowed delta for this specific tick
    float maxDelta = MaxCurrentRamp_A_Per_S_ * _dt_S;

    // 3. Slew-rate limit the Iq command (Torque)
    float iqError = target._TargetIq_A - _ActiveIqCmd_A;
    if (std::abs(iqError) > maxDelta) {
        _ActiveIqCmd_A += (iqError > 0.0f) ? maxDelta : -maxDelta;
    } else {
        _ActiveIqCmd_A = target._TargetIq_A;
    }

    // 4. Slew-rate limit the Id command (Flux / Field Weakening)
    float idError = target._TargetId_A - _ActiveIdCmd_A;
    if (std::abs(idError) > maxDelta) {
        _ActiveIdCmd_A += (idError > 0.0f) ? maxDelta : -maxDelta;
    } else {
        _ActiveIdCmd_A = target._TargetId_A;
    }

    // 5. Clamp to the unified hardware limits from MotorConfig_
    float maxAllowed = MotorConfig_._MaxTorqueCurrent_A; 
    _ActiveIqCmd_A = std::clamp(_ActiveIqCmd_A, -maxAllowed, maxAllowed);
    _ActiveIdCmd_A = std::clamp(_ActiveIdCmd_A, -maxAllowed, maxAllowed);

    // 6. Populate and return the DriveCommand for the inner loop
    DriveCommand cmd;
    cmd._IqCmd_A = _ActiveIqCmd_A;
    cmd._IdCmd_A = _ActiveIdCmd_A;
    cmd._VdFeedforward_V = target._VdFeedforward_V;
    cmd._VqFeedforward_V = target._VqFeedforward_V;
    cmd._VelocityCmd_RadPerSec = 0.0f; // Not used in pure torque mode

    return cmd;
}