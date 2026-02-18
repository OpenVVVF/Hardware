/**
 ***********************************************************************************
 * @file    SwitchingStructs.h
 * @date    2026-02-17
 * @brief   Common set of structs used across many switching applications.
 ***********************************************************************************
 */

#pragma once

/**
 * @brief Scalar voltages for phase u, v, and w.
 * Maps to duty cycle for the three phases.
 */
struct PhaseVoltages {
    float _Du_unitless;  // float from 0-1
    float _Dv_unitless;  // float from 0-1
    float _Dw_unitless;  // float from 0-1
};

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

/**
 * @brief FOC output for external modulation
 */
struct FocOutput {
    float _Valpha_V;             // Stationary frame alpha voltage
    float _Vbeta_V;              // Stationary frame beta voltage
    float _Vdc_V;                // DC bus voltage
    float _ElectricalAngle_Rad;  // For SVM sector calculation
    bool _VoltageLimited;        // True if hit voltage limit
};
