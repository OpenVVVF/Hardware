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
    * allowing torque control over an open-loop V/Hz inner schema.
    */
    PidController VhzVelocityPid_;

    CurrentController() = default;

    /**
    * @brief Resets the active tracking variables and clears PID windup.
    */
    void Reset() override;

    /**
    * @brief Computes simultaneous outputs for FOC (direct current) and V/Hz (cascaded velocity).
    * @param _Sensors Telemetry containing raw phase currents.
    * @param _Target  Polymorphic target, expected to be CurrentSetpoint.
    * @param _dt_S    Time step in seconds.
    * @return DriveCommand populated with both Id/Iq and Velocity targets.
    */
    DriveCommand Update(const SensorData& _Sensors, 
                        const BaseMotionSetpoint& _Target, 
                        float _dt_S) override;

private:
    float _ActiveIqCmd_A = 0.0f;
    float _ActiveIdCmd_A = 0.0f;
};