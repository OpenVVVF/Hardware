/**
***********************************************************************************
* @file    CurrentController.cpp
* @date    2026-02-20
* @brief   Implementation of the outer-loop Current/Torque controller.
***********************************************************************************
*/

#include "CurrentController.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

constexpr float RPM_TO_RAD_PER_SEC = (2.0f * M_PI) / 60.0f;
constexpr float RAD_PER_SEC_TO_RPM = 60.0f / (2.0f * M_PI);

void CurrentController::Reset() {
    _ActiveIqCmd_A = 0.0f;
    _ActiveIdCmd_A = 0.0f;
    VhzVelocityPid_.Reset();
    MaxGovernorPid_.Reset();
    MinGovernorPid_.Reset();
}

DriveCommand CurrentController::Update(const SensorData& _Sensors, 
                                       const BaseMotionSetpoint& _Target, 
                                       float _dt_S) {
                                        
    const CurrentSetpoint& target = static_cast<const CurrentSetpoint&>(_Target);

    float iAlpha = _Sensors._Iu_A;
    float iBeta  = (_Sensors._Iu_A + 2.0f * _Sensors._Iv_A) * 0.577350269f; 
    float measuredCurrentMag_A = std::sqrt(iAlpha * iAlpha + iBeta * iBeta);
    float currentRpm = _Sensors._EncoderVelocity_RadPerSec * RAD_PER_SEC_TO_RPM;

    // =========================================================================
    // 1. PID DYNAMIC LIMIT GOVERNORS
    // =========================================================================
    
    // DEFENSIVE INITIALIZATION: Set gains, bounds, and the new DECIMATION FACTOR
    if (MaxGovernorPid_.Kp_ < 0.001f) {
        MaxGovernorPid_.Kp_ = 2.5f;
        MaxGovernorPid_.Ki_ = 5.9f;
        MaxGovernorPid_.Kd_ = 0.5f;
        MaxGovernorPid_.DecimationFactor_ = 5; // Run 5x slower for stability
        
        MinGovernorPid_.Kp_ = 0.15f;
        MinGovernorPid_.Ki_ = 0.1f;
        MinGovernorPid_.Kd_ = 0.0f;
        MinGovernorPid_.DecimationFactor_ = 3; // Run 5x slower for stability
    }
    
    MaxGovernorPid_.MinOutput_ = 0.0f;
    MaxGovernorPid_.MaxOutput_ = 0.0f;//MotorConfig_._SoftMaxPhaseCurrent_A * 2.0f;
    
    MinGovernorPid_.MinOutput_ = 0.0f;
    MinGovernorPid_.MaxOutput_ = MotorConfig_._SoftMaxPhaseCurrent_A;

    const float k_RpmToAmps = MotorConfig_._SoftMaxPhaseCurrent_A / 500.0f; 

    // --- A. MAX LIMIT GOVERNOR ---
    float maxRpmErr = (currentRpm - MotorConfig_._SoftMaxVelocity_RPM) * k_RpmToAmps;
    float maxIdcErr = _Sensors._Idc_A - MotorConfig_._SoftMaxDcBusCurrent_A;
    float maxMagErr = measuredCurrentMag_A - MotorConfig_._SoftMaxPhaseCurrent_A;

    float govErrMax = std::max(maxRpmErr, std::max(maxIdcErr, maxMagErr));

    // The PID automatically handles decimation caching!
    float penaltyMax_A = 0.0f;//MaxGovernorPid_.Update(govErrMax, 0.0f, _dt_S);
    float allowedMaxIq_A =MotorConfig_._SoftMaxPhaseCurrent_A;// MotorConfig_._SoftMaxPhaseCurrent_A - penaltyMax_A;


    // --- B. MIN LIMIT GOVERNOR ---
    float minRpmErr = (MotorConfig_._SoftMinVelocity_RPM - currentRpm); 
    // float minIdcErr = (-MotorConfig_._SoftMaxRegenCurrent_A - _Sensors._Idc_A);
    // float minMagErr = measuredCurrentMag_A - MotorConfig_._SoftMaxPhaseCurrent_A; 

    // float govErrMin = std::max(minRpmErr, std::max(minIdcErr, minMagErr));

    // MinGovernorPid_.

    // The PID automatically handles decimation caching!
    float penaltyMin_A = 0.0f;//MinGovernorPid_.Update(minRpmErr, 10.0f, _dt_S);
    float allowedMinIq_A = -MotorConfig_._SoftMaxPhaseCurrent_A;//-MotorConfig_._SoftMaxPhaseCurrent_A + penaltyMin_A;


    // --- C. Safety Bounds ---
    if (allowedMinIq_A > allowedMaxIq_A) {
        float midPoint = (allowedMinIq_A + allowedMaxIq_A) * 0.5f;
        allowedMaxIq_A = midPoint;
        allowedMinIq_A = midPoint;
    }
    allowedMaxIq_A = std::min(allowedMaxIq_A, MotorConfig_._SoftMaxPhaseCurrent_A);
    allowedMinIq_A = std::max(allowedMinIq_A, -MotorConfig_._SoftMaxPhaseCurrent_A);

    // =========================================================================
    // 2. TARGET PRE-CONDITIONING
    // =========================================================================
    float boundedTargetIq_A = std::clamp(target._TargetIq_A, allowedMinIq_A, allowedMaxIq_A);
    float boundedTargetId_A = std::clamp(target._TargetId_A, -MotorConfig_._SoftMaxPhaseCurrent_A, MotorConfig_._SoftMaxPhaseCurrent_A);

    float targetMagSq = (boundedTargetIq_A * boundedTargetIq_A) + (boundedTargetId_A * boundedTargetId_A);
    float maxPhaseMagSq = MotorConfig_._SoftMaxPhaseCurrent_A * MotorConfig_._SoftMaxPhaseCurrent_A;
    
    if (targetMagSq > maxPhaseMagSq) {
        float scale = MotorConfig_._SoftMaxPhaseCurrent_A / std::sqrt(targetMagSq);
        boundedTargetIq_A *= scale;
        boundedTargetId_A *= scale;
    }

    // =========================================================================
    // 3. FOC PATH: Direct Current Slew-Rate
    // =========================================================================
    float maxDelta = MaxCurrentRamp_A_Per_S_ * _dt_S;

    float iqError = boundedTargetIq_A - _ActiveIqCmd_A;
    if (std::abs(iqError) > maxDelta) {
        _ActiveIqCmd_A += (iqError > 0.0f) ? maxDelta : -maxDelta;
    } else {
        _ActiveIqCmd_A = boundedTargetIq_A;
    }

    float idError = boundedTargetId_A - _ActiveIdCmd_A;
    if (std::abs(idError) > maxDelta) {
        _ActiveIdCmd_A += (idError > 0.0f) ? maxDelta : -maxDelta;
    } else {
        _ActiveIdCmd_A = boundedTargetId_A;
    }

    // =========================================================================
    // 4. V/HZ PATH: Slip Generation
    // =========================================================================
    float scalarCurrentError = std::abs(_ActiveIqCmd_A) - measuredCurrentMag_A;
    float slipVelocity_RadPerSec = VhzVelocityPid_.Update(scalarCurrentError, 0.0f, _dt_S);

    if (_ActiveIqCmd_A < 0.0f) {
        slipVelocity_RadPerSec = -slipVelocity_RadPerSec;
    }

    float outputVelocity_RadPerSec = _Sensors._EncoderVelocity_RadPerSec + slipVelocity_RadPerSec;

    // =========================================================================
    // 5. POPULATE UNIFIED COMMAND
    // =========================================================================
    DriveCommand cmd;
    cmd._IqCmd_A = _ActiveIqCmd_A;
    cmd._IdCmd_A = _ActiveIdCmd_A;
    cmd._VdFeedforward_V = target._VdFeedforward_V;
    cmd._VqFeedforward_V = target._VqFeedforward_V;
    cmd._VelocityCmd_RadPerSec = outputVelocity_RadPerSec; 

    return cmd;
}