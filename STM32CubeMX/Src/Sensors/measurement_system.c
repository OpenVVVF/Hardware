/**
  ******************************************************************************
  * @file    measurement_system.c
  * @brief   Measurement system implementation.
  ******************************************************************************
  */

#include "measurement_system.h"
#include "adc_backend.h"
#include "main.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define ENC_RAW_MIN_V 0.154f
#define ENC_RAW_MAX_V 0.665f
#define ENC_CENTER_V  ((ENC_RAW_MIN_V + ENC_RAW_MAX_V) * 0.5f)
#define ENC_AMP_V     ((ENC_RAW_MAX_V - ENC_RAW_MIN_V) * 0.5f)
#define ENC_INV_AMP_V (1.0f / ENC_AMP_V)

static float clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static int8_t MeasurementSystem_FindChannel(const MeasurementSystem_t *ms, const char *name)
{
    for (uint8_t i = 0; i < ms->channel_count; i++) {
        if (strcmp(ms->channels[i].config.name, name) == 0) {
            return (int8_t)i;
        }
    }
    return -1;
}

void MeasurementSystem_Init(MeasurementSystem_t *ms)
{
    memset(ms, 0, sizeof(*ms));
    ms->encoder_sin_idx = -1;
    ms->encoder_cos_idx = -1;
    ms->prev_deg = NAN;
    ms->omega_m_rad_s = 0.0f;
}

bool MeasurementSystem_AddChannel(MeasurementSystem_t *ms, const ChannelConfig_t *cfg)
{
    if (ms->channel_count >= MS_MAX_CHANNELS) {
        return false;
    }

    MeasurementChannel_t *ch = &ms->channels[ms->channel_count];
    ch->config = *cfg;
    ch->raw_voltage = 0.0f;
    ch->filtered_value = cfg->offset;
    ch->use_low_pass = (cfg->low_pass_factor > 0.0f && cfg->low_pass_factor < 1.0f);

    if (strcmp(cfg->name, "ENCODER_SIN") == 0) {
        ms->encoder_sin_idx = (int8_t)ms->channel_count;
    } else if (strcmp(cfg->name, "ENCODER_COS") == 0) {
        ms->encoder_cos_idx = (int8_t)ms->channel_count;
    }

    ms->channel_count++;
    return true;
}

float MeasurementSystem_Read(const MeasurementSystem_t *ms, const char *name)
{
    int8_t idx = MeasurementSystem_FindChannel(ms, name);
    if (idx < 0) {
        return 0.0f;
    }
    return ms->channels[idx].filtered_value;
}

static void MeasurementChannel_Update(MeasurementChannel_t *ch, float adc_voltage)
{
    ch->raw_voltage = adc_voltage;
    float physical_value = 0.0f;

    switch (ch->config.type) {
        case SENSOR_TYPE_VOLTAGE_DIVIDER:
            physical_value = adc_voltage * ch->config.scale + ch->config.offset;
            break;
        case SENSOR_TYPE_BIPOLAR_CURRENT:
            physical_value = (adc_voltage - ch->config.zero_offset_volts) * ch->config.scale;
            break;
        case SENSOR_TYPE_UNIPOLAR_CURRENT:
            physical_value = adc_voltage * ch->config.scale + ch->config.offset;
            break;
        case SENSOR_TYPE_TEMPERATURE:
            physical_value = adc_voltage * ch->config.scale + ch->config.offset;
            break;
        case SENSOR_TYPE_THROTTLE:
            physical_value = (adc_voltage - ch->config.offset) * ch->config.scale;
            physical_value = clampf(physical_value, 0.0f, 1.0f);
            break;
        case SENSOR_TYPE_DIRECT:
        default:
            physical_value = adc_voltage + ch->config.offset;
            break;
    }

    if (ch->use_low_pass) {
        ch->filtered_value += (physical_value - ch->filtered_value) * ch->config.low_pass_factor;
    } else {
        ch->filtered_value = physical_value;
    }
}

void MeasurementSystem_Update(MeasurementSystem_t *ms)
{
    for (uint8_t i = 0; i < ms->channel_count; i++) {
        MeasurementChannel_t *ch = &ms->channels[i];
        float voltage = AdcBackend_ReadChannelVoltage(ch->config.name);
        MeasurementChannel_Update(ch, voltage);
    }
}

void MeasurementSystem_CalibrateCurrentSensors(MeasurementSystem_t *ms)
{
    const uint16_t samples = 200;
    float sums[MS_MAX_CHANNELS] = {0};

    for (uint16_t i = 0; i < samples; i++) {
        MeasurementSystem_Update(ms);
        for (uint8_t c = 0; c < ms->channel_count; c++) {
            MeasurementChannel_t *ch = &ms->channels[c];
            if (ch->config.type == SENSOR_TYPE_BIPOLAR_CURRENT ||
                ch->config.type == SENSOR_TYPE_UNIPOLAR_CURRENT) {
                sums[c] += ch->raw_voltage;
            }
        }
        HAL_Delay(2);
    }

    for (uint8_t c = 0; c < ms->channel_count; c++) {
        MeasurementChannel_t *ch = &ms->channels[c];
        if (ch->config.type == SENSOR_TYPE_BIPOLAR_CURRENT ||
            ch->config.type == SENSOR_TYPE_UNIPOLAR_CURRENT) {
            ch->config.zero_offset_volts = sums[c] / (float)samples;
        }
    }
}

float MeasurementSystem_GetRotorPositionDegrees(const MeasurementSystem_t *ms)
{
    if (ms->encoder_sin_idx < 0 || ms->encoder_cos_idx < 0) {
        return NAN;
    }

    float sin_v = ms->channels[ms->encoder_sin_idx].raw_voltage;
    float cos_v = ms->channels[ms->encoder_cos_idx].raw_voltage;

    float sin_n = (sin_v - ENC_CENTER_V) * ENC_INV_AMP_V;
    float cos_n = (cos_v - ENC_CENTER_V) * ENC_INV_AMP_V;

    sin_n = clampf(sin_n, -1.2f, 1.2f);
    cos_n = clampf(cos_n, -1.2f, 1.2f);

    float angle_rad = atan2f(sin_n, cos_n);
    float angle_deg = angle_rad * 180.0f / M_PI;
    if (angle_deg < 0.0f) {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

static float wrap_delta_deg(float delta_deg)
{
    delta_deg = fmodf(delta_deg + 180.0f, 360.0f);
    if (delta_deg < 0.0f) {
        delta_deg += 360.0f;
    }
    return delta_deg - 180.0f;
}

float MeasurementSystem_GetRotorOmegaMechanicalRadPerSec(MeasurementSystem_t *ms, float dt_s)
{
    if (dt_s <= 0.0f) {
        return NAN;
    }

    float deg = MeasurementSystem_GetRotorPositionDegrees(ms);
    if (!isfinite(deg)) {
        return NAN;
    }

    if (!isfinite(ms->prev_deg)) {
        ms->prev_deg = deg;
        ms->omega_m_rad_s = 0.0f;
        return ms->omega_m_rad_s;
    }

    float ddeg = wrap_delta_deg(deg - ms->prev_deg);
    ms->prev_deg = deg;

    float drad = ddeg * (float)M_PI / 180.0f;
    ms->omega_m_rad_s = drad / dt_s;
    return ms->omega_m_rad_s;
}
