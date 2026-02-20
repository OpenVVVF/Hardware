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
    VhzVelocityPid_.Reset();
}

DriveCommand CurrentController::Update(const SensorData& _Sensors, 
                                       const BaseMotionSetpoint& _Target, 
                                       float _dt_S) {
                                        
    const CurrentSetpoint& target = static_cast<const CurrentSetpoint&>(_Target);

    // =========================================================================
    // 1. FOC PATH: Direct Current Slew-Rate Limiter
    // =========================================================================
    float maxDelta = MaxCurrentRamp_A_Per_S_ * _dt_S;

    // Slew-rate limit Iq
    float iqError = target._TargetIq_A - _ActiveIqCmd_A;
    if (std::abs(iqError) > maxDelta) {
        _ActiveIqCmd_A += (iqError > 0.0f) ? maxDelta : -maxDelta;
    } else {
        _ActiveIqCmd_A = target._TargetIq_A;
    }

    // Slew-rate limit Id
    float idError = target._TargetId_A - _ActiveIdCmd_A;
    if (std::abs(idError) > maxDelta) {
        _ActiveIdCmd_A += (idError > 0.0f) ? maxDelta : -maxDelta;
    } else {
        _ActiveIdCmd_A = target._TargetId_A;
    }

    // Clamp to unified hardware limits
    float maxAllowed = MotorConfig_._MaxTorqueCurrent_A; 
    _ActiveIqCmd_A = std::clamp(_ActiveIqCmd_A, -maxAllowed, maxAllowed);
    _ActiveIdCmd_A = std::clamp(_ActiveIdCmd_A, -maxAllowed, maxAllowed);


    // =========================================================================
    // 2. V/HZ PATH: Cascaded PID (Current -> Velocity Translation)
    // =========================================================================
    // Reconstruct current magnitude using a quick Clarke transform on the sensors
    float iAlpha = _Sensors._Iu_A;
    float iBeta = (_Sensors._Iu_A + 2.0f * _Sensors._Iv_A) * 0.577350269f; // 1/sqrt(3)
    float measuredCurrentMag = std::sqrt(iAlpha * iAlpha + iBeta * iBeta);

    // Calculate error based on absolute requested torque (direction is handled later)
    float scalarCurrentError = std::abs(target._TargetIq_A) - measuredCurrentMag;

    // PID computes the necessary velocity/slip adjustment to draw the target current
    float velocityAdjustment = VhzVelocityPid_.Update(scalarCurrentError, 0.0f, _dt_S);

    // Apply the requested direction to the calculated velocity
    float outputVelocity = (target._TargetIq_A >= 0.0f) ? velocityAdjustment : -velocityAdjustment;

    // Clamp to hardware velocity limits
    outputVelocity = std::clamp(outputVelocity, 
                                -MotorConfig_._MaxVelocity_RadPerSec, 
                                 MotorConfig_._MaxVelocity_RadPerSec);


    // =========================================================================
    // 3. POPULATE UNIFIED COMMAND
    // =========================================================================
    DriveCommand cmd;
    
    // FOC consumes these:
    cmd._IqCmd_A = _ActiveIqCmd_A;
    cmd._IdCmd_A = _ActiveIdCmd_A;
    cmd._VdFeedforward_V = target._VdFeedforward_V;
    cmd._VqFeedforward_V = target._VqFeedforward_V;
    
    // V/Hz consumes this:
    cmd._VelocityCmd_RadPerSec = outputVelocity; 

    return cmd;
}