/**
  ******************************************************************************
  * @file    pwm_driver.c
  * @brief   TIM1 PWM driver wrapper implementation.
  ******************************************************************************
  */

#include "pwm_driver.h"
#include "tim.h"
#include "gate_driver.h"

#include <math.h>

static float clamp_duty(float x, float min, float max)
{
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

void PWMDriver_Init(PWMDriver_t *drv, float carrier_hz, float min_duty_percent, float max_duty_percent)
{
    drv->min_duty_percent = min_duty_percent;
    drv->max_duty_percent = max_duty_percent;
    drv->carrier_hz = carrier_hz;
    drv->enabled = false;
    drv->emergency_stop = false;

    PWMDriver_SetCarrierFrequency(drv, carrier_hz);
    PWMDriver_SetDutyCycles(drv, 50.0f, 50.0f, 50.0f);
}

void PWMDriver_Enable(PWMDriver_t *drv)
{
    if (drv->emergency_stop) {
        return;
    }
    drv->enabled = true;
    PWM_Start();
}

void PWMDriver_Disable(PWMDriver_t *drv)
{
    drv->enabled = false;
    PWM_Stop();
}

void PWMDriver_EmergencyStop(PWMDriver_t *drv)
{
    drv->emergency_stop = true;
    drv->enabled = false;
    PWM_Stop();
    PWM_ClearFault();  /* clear break flag, but keep MOE low */
    GateDriver_ResetPulse();
}

void PWMDriver_ClearEmergency(PWMDriver_t *drv)
{
    drv->emergency_stop = false;
    PWM_ClearFault();
}

void PWMDriver_SetCarrierFrequency(PWMDriver_t *drv, float hz)
{
    if (hz <= 0.0f) {
        return;
    }
    drv->carrier_hz = hz;
    PWM_SetFrequency((uint32_t)hz);
}

void PWMDriver_SetDutyCycles(PWMDriver_t *drv, float du, float dv, float dw)
{
    du = clamp_duty(du, drv->min_duty_percent, drv->max_duty_percent);
    dv = clamp_duty(dv, drv->min_duty_percent, drv->max_duty_percent);
    dw = clamp_duty(dw, drv->min_duty_percent, drv->max_duty_percent);

    PWM_SetDutyCycle(0, du);
    PWM_SetDutyCycle(1, dv);
    PWM_SetDutyCycle(2, dw);
}

void PWMDriver_SetHardwareCommand(PWMDriver_t *drv, const HardwareCommand_t *cmd)
{
    PWMDriver_SetCarrierFrequency(drv, cmd->SwitchingFrequency_Hz);
    PWMDriver_SetDutyCycles(drv,
                            cmd->DutyPhU_unitless * 100.0f,
                            cmd->DutyPhV_unitless * 100.0f,
                            cmd->DutyPhW_unitless * 100.0f);
}
