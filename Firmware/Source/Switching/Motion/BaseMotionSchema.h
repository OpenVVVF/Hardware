/**
***********************************************************************************
* @file    BaseMotionSchema.h
* @date    2026-02-19
* @brief   Abstract base class for outer-loop motion strategies.
***********************************************************************************
*/

#pragma once

#include "../SwitchingCommon.h"
#include "../Control/ControlInput.h"

/**
* @brief Empty base struct for motion setpoints.
* Derived controllers will extend this to add Position, Velocity, or Torque targets.
*/
struct BaseMotionSetpoint {
    virtual ~BaseMotionSetpoint() = default; 
};

/**
* @brief Abstract base class representing an outer-loop motion strategy.
*/
class MotionController {

    public:
        MotorConfig MotorConfig_; ///< Unified hardware and software physical limits

        virtual ~MotionController() = default;

        /**
        * @brief Core update loop to be overridden by the specific motion scheme.
        * @param _Sensors Telemetry from the common header.
        * @param _Target  Polymorphic target command (must be cast by the derived class).
        * @param _dt_S    Time step since last execution in seconds.
        * @return DriveCommand to be routed to inner-loop control schemas.
        */
        virtual DriveCommand Update(const SensorData& _Sensors, 
                                    const BaseMotionSetpoint& _Target, 
                                    float _dt_S) = 0;

        /**
        * @brief Resets internal state (PID integrators, etc.) to prevent windup.
        */
        virtual void Reset() = 0;

        /**
        * @brief Sets motor-specific hardware and safety limits.
        * @param _MotorConfig The unified hardware configuration struct.
        */
        void SetMotorConfig(const MotorConfig& _MotorConfig);
};