/**
***********************************************************************************
* @file    DriveManager.h
* @date    2026-02-19
* @brief   Top-level orchestrator for the motor control pipeline. 
* Handles the decimation of the outer-loop (Motion/Current) relative 
* to the inner-loop (FOC/Modulation) to ensure PID stability.
***********************************************************************************
*/

#pragma once

#include "Motion/BaseMotionSchema.h"
#include "Control/ControlSelector.h"
#include "Modulation/ModulationSelector.h"

/**
 * @brief Manages the execution flow from high-level setpoints to PWM duty cycles.
 */
class DriveManager {
public:
    DriveManager() = default;

    /**
     * @brief Assigns the motion strategy (e.g., CurrentController, VelocityController).
     * @param _motionController Pointer to the motion strategy instance.
     */
    void SetMotionController(MotionController* _motionController);

    /**
     * @brief Configures the execution ratio between the inner and outer loops.
     * @param _ratio Number of inner-loop ticks per one outer-loop tick (e.g., 10).
     */
    void SetMotionUpdateRatio(uint16_t _ratio);

    /**
     * @brief Registers an inner-loop control scheme (e.g., FOC, V/Hz).
     */
    void RegisterControlScheme(ControlScheme* _scheme);

    /**
     * @brief Registers a modulation scheme (e.g., SPWM, N-Pulse).
     */
    void RegisterModulationScheme(ModulationScheme* _scheme);

    /**
     * @brief Executes the complete control pipeline.
     * @param _Sensors Standardized system state telemetry.
     * @param _Setpoint Polymorphic target (e.g., CurrentSetpoint).
     * @param _dt_S The high-speed time step in seconds.
     * @return HardwareCommand containing final duty cycles and carrier frequency.
     */
    HardwareCommand Update(const SensorData& _Sensors, 
                           const BaseMotionSetpoint& _Setpoint, 
                           float _dt_S);

private:
    MotionController* MotionController_ = nullptr;
    ControlSelector     ControlSelector_;
    ModulationSelector  ModulationSelector_;

    // Execution Decimation State
    uint16_t MotionUpdateRatio_   = 10;     ///< Default 10x separation
    uint16_t MotionUpdateCounter_ = 0;      ///< Ticks since last motion update
    float AccumulatedMotionDt_S_  = 0.0f;   ///< Total time elapsed for motion loop
    DriveCommand CachedDriveCmd_  = {0};    ///< Latched command for high-speed loops
};