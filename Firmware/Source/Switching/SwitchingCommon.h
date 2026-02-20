/**
***********************************************************************************
* @file    SwitchingCommon.h
* @date    2026-02-19
* @brief   Core data structures shared across all motor control abstraction layers
* (Motion, Control, and Modulation).
***********************************************************************************
*/

#pragma once

/**
* @brief Motor, inverter, and hardware configuration.
* Single source of truth for all physical and software safety limits.
*/
struct MotorConfig {
    float _PolePairs_unitless;
    float _Ld_Henry;
    float _Lq_Henry;
    float _FluxLinkage_Wb;
    
    // Hardware Electrical Limits
    float _MaxPhaseCurrent_A;
    float _ContinuousPhaseCurrent_A;
    float _MaxDcBusCurrent_A;
    float _MaxRegenCurrent_A;
    float _DcBusVoltage_V;
    float _MaxModulation_unitless;

    // Outer-Loop Motion Limits
    float _MaxTorqueCurrent_A;
    float _MaxRpm_unitless;
    float _MinRpm_unitless;
    float _MaxVelocity_RadPerSec;
    float _MaxAcceleration_RadPerSec2;
};


/**
* @brief Sensor inputs and telemetry.
* Passed into every Update tick for state feedback.
*/
struct SensorData {
    float _Iu_A;
    float _Iv_A;
    float _Iw_A;
    float _Idc_A;
    float _EncoderPosition_Rad;
    float _EncoderVelocity_RadPerSec;
    float _DcBusVoltage_V;
};

