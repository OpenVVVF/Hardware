/**
***********************************************************************************
* @file    BaseSchema.h
* @date    2026-02-19
* @brief   Abstract base class for all inner-loop control strategies (FOC, V/Hz).
* Defines the unified interface for calculating stationary frame voltages.
***********************************************************************************
*/

#pragma once

#include "../ControlInput.h"
#include "../../Modulation/ModulationInput.h"

/**
* @brief Common configuration shared by ALL inner-loop control schemes.
* Defines the velocity bounds where the scheme is considered active.
*/
struct ControlCommonConfig {
    float InfluenceStart_RadPerSec_ = 0.0f; ///< Minimum velocity for scheme activation
    float InfluenceEnd_RadPerSec_   = 0.0f; ///< Maximum velocity for scheme activation
};

/**
* @brief Abstract base class representing a commutation/control strategy.
*
* All control schemes (FOC, V/Hz, DTC) inherit from this class. It provides
* a standardized `Update` loop that accepts outer-loop DriveCommands and
* outputs generic ModulationInputs for the hardware driver.
*/
class ControlScheme {

    public:
        ControlCommonConfig Config_;  ///< Active velocity zone configuration
        MotorConfig MotorConfig_;     ///< Hardware limits and physical motor properties

        virtual ~ControlScheme() = default;

        /**
        * @brief Indicates if this scheme requires a bumpless hard transfer.
        * @return true by default, as fading between open/closed loop vectors is unsafe.
        */
        virtual bool RequiresHardTransition() { 
            return true; 
        }

        /**
        * @brief Core update loop to be overridden by the specific control scheme.
        * @param _Sensors Snapshot of system telemetry (currents, voltages, angles).
        * @param _Cmd     Explicit drive targets (Torque, Flux, Speed) from motion controller.
        * @param _dt_S    Time step since last execution in seconds.
        * @return ModulationInput containing required Valpha/Vbeta and synchronization angle.
        */
        virtual ModulationInput Update(const SensorData& _Sensors, 
                                        const DriveCommand& _Cmd, 
                                        float _dt_S) = 0;

        /**
        * @brief Resets internal state variables (like PI integrators).
        * Must be called prior to transitioning into this scheme to prevent windup.
        */
        virtual void Reset() = 0;

        /**
        * @brief Applies common configuration limits to the scheme.
        * @param _Config Configuration struct defining active velocity zones.
        */
        void ApplyConfig(ControlCommonConfig _Config);

        /**
        * @brief Sets motor-specific hardware limits and physical parameters.
        * This is non-virtual to guarantee consistent limits across all schemas.
        * @param _MotorConfig The hardware configuration struct.
        */
        void SetMotorConfig(const MotorConfig& _MotorConfig);

        /**
        * @brief Helper to determine if the scheme should be active at a given speed.
        * @param _Velocity_RadPerSec Current absolute mechanical/electrical velocity.
        * @param _TransitionWindow_RadPerSec Hysteresis buffer to prevent chattering.
        * @return true if the velocity falls within the scheme's influence zone.
        */
        bool IsActiveAtVelocity(float _Velocity_RadPerSec, float _TransitionWindow_RadPerSec = 0.0f);
};