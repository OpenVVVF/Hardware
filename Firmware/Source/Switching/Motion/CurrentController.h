/**
***********************************************************************************
* @file    CurrentController.h
* @date    2026-02-19
* @brief   Outer-loop Current/Torque controller. Acts as a slew-rate limiter 
* and safe pass-through to the inner FOC control loop.
***********************************************************************************
*/

#pragma once

#include "BaseMotionSchema.h"

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
* @brief Controller for mapping current requests to the inner drive loop.
*/
class CurrentController : public MotionController {
public:
    float MaxCurrentRamp_A_Per_S_ = 500.0f; ///< Max allowed rate of change for commanded current

    CurrentController() = default;

    /**
    * @brief Resets the active tracking variables to zero.
    */
    void Reset() override;

    /**
    * @brief Slew-rate limits the target currents and clamps them to hardware limits.
    * @param _Sensors Telemetry (not strictly needed for open-loop ramping, but matches signature).
    * @param _Target  Polymorphic target, expected to be CurrentSetpoint.
    * @param _dt_S    Time step in seconds.
    * @return DriveCommand populated with Id/Iq targets for FOC.
    */
    DriveCommand Update(const SensorData& _Sensors, 
                        const BaseMotionSetpoint& _Target, 
                        float _dt_S) override;

private:
    float _ActiveIqCmd_A = 0.0f;
    float _ActiveIdCmd_A = 0.0f;
};