/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.h
  * @brief   This file contains all the function prototypes for
  *          the tim.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __TIM_H__
#define __TIM_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern TIM_HandleTypeDef htim1;

/* USER CODE BEGIN Private defines */
#define PWM_DEFAULT_SWITCHING_FREQ_HZ   1000U
#define PWM_DEFAULT_DEADTIME_NS         1000U   /* 1 us */
/* USER CODE END Private defines */

void MX_TIM1_Init(void);

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* USER CODE BEGIN Prototypes */
void PWM_SetFrequency(uint32_t freq_hz);
void PWM_SetDeadTime(uint32_t deadtime_ns);
void PWM_SetDutyCycle(uint8_t phase, float duty_percent);
void PWM_SetThreePhaseDuty(float duty_u, float duty_v, float duty_w);
void PWM_StartSPWM(float fundamental_freq_hz, float modulation_index);
void PWM_StopSPWM(void);
void PWM_SetSPWMParams(float fundamental_freq_hz, float modulation_index);
void PWM_PrintSPWMState(void);
void PWM_StartPhase(uint8_t phase);
void PWM_StopPhase(uint8_t phase);
void PWM_Start(void);
void PWM_Stop(void);
void PWM_ClearFault(void);
void PWM_PrintState(void);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __TIM_H__ */

