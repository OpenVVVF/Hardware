/**
***********************************************************************************
* @file    CurrentController.h
* @date    2026-02-19
* @brief   Outer-loop Current/Torque controller. 
* Provides direct Iq/Id targets for FOC, and dynamically computes a 
* Velocity target for scalar V/Hz control using a cascaded PID loop.
***********************************************************************************
*/

#pragma once

#include "BaseMotionSchema.h"
#include "Utils/Control/PIDController.h"

/**
* @brief Specific setpoint for Current/Torque control.
*/
struct CurrentSetpoint : public BaseMotionSetpoint {
    float _TargetIq_A = 0.0f;
    float _TargetId_A = 0.0f;
    float _VqFeedforward_V = 0.0f;
    float _VdFeedforward_V = 0.0f;
};

/**
* @brief Controller for mapping current requests to the inner drive loops.
*/
class CurrentController : public MotionController {
public:
    float MaxCurrentRamp_A_Per_S_ = 500.0f; ///< Slew rate limit for FOC targets

    /** * @brief Cascaded PID to translate a Current Error into a Velocity Command 
    */
    PidController VhzVelocityPid_;

    // ========================================================
    // NEW: PID Limit Governors
    // ========================================================
    PidController MaxGovernorPid_; ///< Chokes positive current limits
    PidController MinGovernorPid_; ///< Chokes negative (regen) current limits

    CurrentController() = default;

    void Reset() override;

    DriveCommand Update(const SensorData& _Sensors, 
                        const BaseMotionSetpoint& _Target, 
                        float _dt_S) override;

private:
    float _ActiveIqCmd_A = 0.0f;
    float _ActiveIdCmd_A = 0.0f;
};