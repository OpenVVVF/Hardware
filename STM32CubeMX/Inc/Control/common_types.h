/**
  ******************************************************************************
  * @file    common_types.h
  * @brief   Shared data structures for the motor-control stack.
  *          Ported from the Raspberry Pi Pico firmware and adapted for C11.
  ******************************************************************************
  */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Sensor / measurement
 * ============================================================================ */

typedef enum {
    SENSOR_TYPE_VOLTAGE_DIVIDER,
    SENSOR_TYPE_BIPOLAR_CURRENT,
    SENSOR_TYPE_UNIPOLAR_CURRENT,
    SENSOR_TYPE_TEMPERATURE,
    SENSOR_TYPE_THROTTLE,
    SENSOR_TYPE_DIRECT
} SensorType_t;

typedef struct {
    size_t  device_index;
    uint8_t channel;
    SensorType_t type;
    float   scale;
    float   offset;
    float   low_pass_factor;
    char    name[16];
    float   zero_offset_volts;
} ChannelConfig_t;

/* ============================================================================
 * Motor and hardware configuration
 * ============================================================================ */

typedef struct {
    float _PolePairs_unitless;
    float _Ld_Henry;
    float _Lq_Henry;
    float _FluxLinkage_Wb;

    float _HardMaxPhaseCurrent_A;
    float _SoftMaxPhaseCurrent_A;

    float _HardMaxDcBusCurrent_A;
    float _SoftMaxDcBusCurrent_A;

    float _HardMaxRegenCurrent_A;
    float _SoftMaxRegenCurrent_A;

    float _MaxModulation_unitless;

    float _HardMaxVelocity_RPM;
    float _SoftMaxVelocity_RPM;
    float _HardMinVelocity_RPM;
    float _SoftMinVelocity_RPM;

    float _MaxAcceleration_RadPerSec2;
} MotorConfig_t;

/* ============================================================================
 * Feedback and commands
 * ============================================================================ */

typedef struct {
    float _Iu_A;
    float _Iv_A;
    float _Iw_A;
    float _Idc_A;
    float _EncoderPosition_Rad;
    float _EncoderVelocity_RadPerSec;
    float _DcBusVoltage_V;
} SensorData_t;

typedef struct {
    float _IqCmd_A;
    float _IdCmd_A;
    float _VdFeedforward_V;
    float _VqFeedforward_V;
    float _VelocityCmd_RadPerSec;
} DriveCommand_t;

typedef struct {
    float _TargetIq_A;
    float _TargetId_A;
    float _VqFeedforward_V;
    float _VdFeedforward_V;
} CurrentSetpoint_t;

typedef struct {
    float Valpha_V;
    float Vbeta_V;
    float Vdc_V;
    float Theta_Rad;
    float Omega_RadPerSec;
} ModulationInput_t;

typedef struct {
    float SwitchingFrequency_Hz;
    float DutyPhU_unitless;
    float DutyPhV_unitless;
    float DutyPhW_unitless;
} HardwareCommand_t;

/* ============================================================================
 * Fault management
 * ============================================================================ */

#define MAX_ACTIVE_FAULTS 16
#define FAULT_DESC_LENGTH 48

typedef enum {
    FAULT_SEVERITY_WARNING,
    FAULT_SEVERITY_SELF_CLEARING,
    FAULT_SEVERITY_LATCHED
} FaultSeverity_t;

typedef struct {
    char Description[FAULT_DESC_LENGTH];
    FaultSeverity_t Severity;
    float TimeRemaining_S;
} FaultRecord_t;

#ifdef __cplusplus
}
#endif
