/**
 ***********************************************************************************
 * @file    SwitchingStructs.h
 * @date    2026-02-17
 * @brief   Common set of structs used across many switching applications.
 ***********************************************************************************
 */

#pragma once


/**
 * @brief Motor and inverter configuration
 */
struct MotorConfig {
    float _PolePairs_unitless;
    float _Ld_Henry;
    float _Lq_Henry;
    float _FluxLinkage_Wb;
    float _MaxPhaseCurrent_A;
    float _ContinuousPhaseCurrent_A;
    float _MaxDcBusCurrent_A;
    float _MaxRegenCurrent_A;
    float _MaxRpm_unitless;
    float _MinRpm_unitless;
    float _MaxModulation_unitless;
    float _DcBusVoltage_V;
};

/**
 * @brief Sensor inputs - also used for FOC state to avoid duplication
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

/**
 * @brief Current command input
 */
struct CurrentCommand {
    float _IdCmd_A;  // D-axis current command (flux)
    float _IqCmd_A;  // Q-axis current command (torque)
};

