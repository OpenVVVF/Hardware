/**
***********************************************************************************
* @file    BaseSchema.h
* @date    2026-02-19
* @brief   Abstract base class for all inner-loop control strategies (FOC, V/Hz).
***********************************************************************************
*/

#pragma once

#include "../ControlInput.h"
#include "../../Modulation/ModulationInput.h"

/**
* @brief Common configuration shared by ALL control schemes.
*/
struct ControlCommonConfig {
    float InfluenceStart_RadPerSec_ = 0.0f; // Start influence range
    float InfluenceEnd_RadPerSec_ = 0.0f;   // End influence range
};

/**
* @brief   Abstract base class for all commutation strategies (FOC, V/Hz, etc).
*/
class ControlScheme {

    public:
        ControlCommonConfig Config_;
        MotorConfig MotorConfig_; // Shared across all derived schemes

        virtual ~ControlScheme() = default;

        virtual bool RequiresHardTransition() { 
            return true; 
        }

        virtual ModulationInput Update(const SensorData& _Sensors, const DriveCommand& _Cmd, float _dt_S) = 0;

        virtual void Reset() = 0;

        /**
        * @brief Applies common configuration object.
        */
        void ApplyConfig(ControlCommonConfig _Config);

        /**
        * @brief Sets motor-specific hardware limits. 
        * Non-virtual: Enforces consistent behavior across all schemes.
        */
        void SetMotorConfig(const MotorConfig& _MotorConfig);

        /**
        * @brief Helper to return true/false if this function is active at this velocity.
        */
        bool IsActiveAtVelocity(float _Velocity_RadPerSec, float _TransitionWindow_RadPerSec = 0.0f);
};