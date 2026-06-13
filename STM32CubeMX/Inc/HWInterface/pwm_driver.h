/**
  ******************************************************************************
  * @file    pwm_driver.h
  * @brief   C port of the PWMDriver for STM32 TIM1.
  *          Wraps the generated TIM1 PWM functions with a safety-oriented API.
  ******************************************************************************
  */

#pragma once

#include "Control/common_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float min_duty_percent;
    float max_duty_percent;
    float carrier_hz;
    bool enabled;
    bool emergency_stop;
} PWMDriver_t;

void PWMDriver_Init(PWMDriver_t *drv, float carrier_hz, float min_duty_percent, float max_duty_percent);

void PWMDriver_Enable(PWMDriver_t *drv);
void PWMDriver_Disable(PWMDriver_t *drv);
void PWMDriver_EmergencyStop(PWMDriver_t *drv);
void PWMDriver_ClearEmergency(PWMDriver_t *drv);

void PWMDriver_SetCarrierFrequency(PWMDriver_t *drv, float hz);
void PWMDriver_SetDutyCycles(PWMDriver_t *drv, float du, float dv, float dw);
void PWMDriver_SetHardwareCommand(PWMDriver_t *drv, const HardwareCommand_t *cmd);

static inline bool PWMDriver_IsEnabled(const PWMDriver_t *drv)
{
    return drv->enabled;
}

static inline bool PWMDriver_IsEmergencyStopped(const PWMDriver_t *drv)
{
    return drv->emergency_stop;
}

#ifdef __cplusplus
}
#endif
