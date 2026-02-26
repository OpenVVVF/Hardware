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

#include "Utils/Control/PIDController.h"

#include "Utils/Fault/FaultManager.h"

/**
 * @brief Manages the execution flow from high-level setpoints to PWM duty cycles.
 */

class CalibrationManager {
public:
    CalibrationManager() = default;
    enum CalibrationMode {
        ENCODER_MINMAX, ENCODER_OFFSET, LR, FL, IDLE
    };

    void SetMode(CalibrationMode mode) {m_mode = mode;};
    bool Update(FaultManager* _FaultManager, MotorConfig* _MotorConfig, PWMDriver* _Driver, const SensorData& _Sensors, float _DT);

    float GetEncoderOffset_Rad() {return m_encoderCalib.MeasuredOffset_Rad; } 
    CalibrationMode GetMode() {return m_mode; }

private:
    CalibrationMode m_mode;


    struct EncoderCalibrationContext {
        enum class State { INIT, WAIT_SETTLE, SAMPLE, DONE };
        State CurrentState = State::INIT;
        
        float Timer_sec = 0.0f;
        float MeasuredOffset_Rad = 0.0f;
    
        // Configurable constraints
        float TargetAlignCurrent_A = 5.0f; // Lowered slightly, watch that DC bus drop!
        float SettleTime_sec = 2.0f;        
        float VelocityThreshold = 0.1f;     
    
        // --- NEW: Multi-sampling & Wrapping ---
        float SampleTime_sec = 0.5f;        // How long to spend accumulating samples
        float Accumulator = 0.0f;
        uint32_t SampleCount = 0;
        static constexpr float TWO_PI = 6.28318530718f;
    
        PidController CurrentPid;
    } m_encoderCalib;


};