/**
***********************************************************************************
* @file    CalibrationManager.h
* @date    2026-02-19
* @brief   Top-level orchestrator for the motor control pipeline. 
* Handles the decimation of the outer-loop (Motion/Current) relative 
* to the inner-loop (FOC/Modulation) to ensure PID stability.
***********************************************************************************
*/

#pragma once

#include "Motion/BaseMotionSchema.h"
#include "Control/Schemas/BaseSchema.h" 
#include "Modulation/ModulationSelector.h"
#include "HWInterface/PWMDriver.h"

#include "Utils/Fault/FaultManager.h"

/**
 * @brief Manages the execution flow from high-level setpoints to PWM duty cycles.
 */
class CalibrationManager {
public:
    CalibrationManager() = default;

    bool Update(FaultManager* _FaultManager, PWMDriver* _Driver, const SensorData& _Sensors);

private:
   
};