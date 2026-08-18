/**
  ******************************************************************************
  * @file    measurement_system.h
  * @brief   C port of the Pico MeasurementSystem.
  ******************************************************************************
  */

#pragma once

#include "Control/common_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MS_MAX_CHANNELS     16U
#define MS_MAX_NAME_LEN     16U

typedef struct {
    ChannelConfig_t config;
    float raw_voltage;
    float filtered_value;
    bool use_low_pass;
} MeasurementChannel_t;

typedef struct {
    MeasurementChannel_t channels[MS_MAX_CHANNELS];
    uint8_t channel_count;
    int8_t encoder_sin_idx;
    int8_t encoder_cos_idx;
    float prev_deg;
    float omega_m_rad_s;
} MeasurementSystem_t;

void MeasurementSystem_Init(MeasurementSystem_t *ms);
bool MeasurementSystem_AddChannel(MeasurementSystem_t *ms, const ChannelConfig_t *cfg);
float MeasurementSystem_Read(const MeasurementSystem_t *ms, const char *name);
void MeasurementSystem_Update(MeasurementSystem_t *ms);
void MeasurementSystem_CalibrateCurrentSensors(MeasurementSystem_t *ms);
float MeasurementSystem_GetRotorPositionDegrees(const MeasurementSystem_t *ms);
float MeasurementSystem_GetRotorOmegaMechanicalRadPerSec(MeasurementSystem_t *ms, float dt_s);

#ifdef __cplusplus
}
#endif
