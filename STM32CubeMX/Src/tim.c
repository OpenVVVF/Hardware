/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.c
  * @brief   This file provides code for the configuration
  *          of the TIM instances.
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
/* Includes ------------------------------------------------------------------*/
#include "tim.h"

/* USER CODE BEGIN 0 */
#define TIM1_CLOCK_HZ       275000000UL
#define PWM_DEADTIME_NS     1000U   /* target ~3 us (max with DIV1 = ~3.67 us) */
#define TIM_MAX_ARR         65535U

/* CKD = DIV1  =>  t_DTS = 1 / 275 MHz = 3.636 ns
   Use DTG[7:5] = 111 encoding: DT = (32 + DTG[4:0]) * 16 * t_DTS  */
static uint8_t PWM_ComputeDeadTime(uint32_t deadtime_ns)
{
    uint32_t val = (deadtime_ns * 275ULL + 4000ULL) / 8000ULL;
    if (val < 32) val = 32;
    if (val > 63) val = 63;
    return (uint8_t)(0xE0 | (val - 32));
}

static uint32_t PWM_PhaseToChannel(uint8_t phase)
{
    switch (phase)
    {
        case 0: return TIM_CHANNEL_1;
        case 1: return TIM_CHANNEL_2;
        case 2: return TIM_CHANNEL_3;
        default: return 0;
    }
}
/* USER CODE END 0 */

TIM_HandleTypeDef htim1;

/* TIM1 init function */
void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIMEx_BreakInputConfigTypeDef sBreakInputConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
  htim1.Init.Period = 13750;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakInputConfig.Source = TIM_BREAKINPUTSOURCE_BKIN;
  sBreakInputConfig.Enable = TIM_BREAKINPUTSOURCE_ENABLE;
  sBreakInputConfig.Polarity = TIM_BREAKINPUTSOURCE_POLARITY_HIGH;
  if (HAL_TIMEx_ConfigBreakInput(&htim1, TIM_BREAKINPUT_BRK, &sBreakInputConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_ENABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_ENABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = PWM_ComputeDeadTime(PWM_DEADTIME_NS);
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_ENABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_LOW;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef* tim_pwmHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(tim_pwmHandle->Instance==TIM1)
  {
  /* USER CODE BEGIN TIM1_MspInit 0 */

  /* USER CODE END TIM1_MspInit 0 */
    /* TIM1 clock enable */
    __HAL_RCC_TIM1_CLK_ENABLE();

    __HAL_RCC_GPIOE_CLK_ENABLE();
    /**TIM1 GPIO Configuration
    PE15     ------> TIM1_BKIN
    */
    GPIO_InitStruct.Pin = GATE_DRIVER_FAULT_PWM_BREAK_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;  /* /FLT is open-drain */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GATE_DRIVER_FAULT_PWM_BREAK_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN TIM1_MspInit 1 */

  /* USER CODE END TIM1_MspInit 1 */
  }
}
void HAL_TIM_MspPostInit(TIM_HandleTypeDef* timHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(timHandle->Instance==TIM1)
  {
  /* USER CODE BEGIN TIM1_MspPostInit 0 */

  /* USER CODE END TIM1_MspPostInit 0 */

    __HAL_RCC_GPIOE_CLK_ENABLE();
    /**TIM1 GPIO Configuration
    PE8     ------> TIM1_CH1N
    PE9     ------> TIM1_CH1
    PE10     ------> TIM1_CH2N
    PE11     ------> TIM1_CH2
    PE12     ------> TIM1_CH3N
    PE13     ------> TIM1_CH3
    */
    GPIO_InitStruct.Pin = PH_U_HIGH_Pin|PH_U_LOW_Pin|PH_V_HIGH_Pin|PH_V_LOW_Pin
                          |PH_W_HIGH_Pin|PH_W_LOW_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* USER CODE BEGIN TIM1_MspPostInit 1 */

  /* USER CODE END TIM1_MspPostInit 1 */
  }

}

void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef* tim_pwmHandle)
{

  if(tim_pwmHandle->Instance==TIM1)
  {
  /* USER CODE BEGIN TIM1_MspDeInit 0 */

  /* USER CODE END TIM1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_TIM1_CLK_DISABLE();

    /**TIM1 GPIO Configuration
    PE8     ------> TIM1_CH1N
    PE9     ------> TIM1_CH1
    PE10     ------> TIM1_CH2N
    PE11     ------> TIM1_CH2
    PE12     ------> TIM1_CH3N
    PE13     ------> TIM1_CH3
    PE15     ------> TIM1_BKIN
    */
    HAL_GPIO_DeInit(GPIOE, PH_U_HIGH_Pin|PH_U_LOW_Pin|PH_V_HIGH_Pin|PH_V_LOW_Pin
                          |PH_W_HIGH_Pin|PH_W_LOW_Pin|GATE_DRIVER_FAULT_PWM_BREAK_Pin);

  /* USER CODE BEGIN TIM1_MspDeInit 1 */

  /* USER CODE END TIM1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

void PWM_SetFrequency(uint32_t freq_hz)
{
    if (freq_hz == 0) return;

    /* Center-aligned: f = f_tim / ((PSC+1) * 2 * ARR)
       ARR is 16-bit (max 65535), so we may need to raise PSC. */
    uint32_t target = TIM1_CLOCK_HZ / (2UL * freq_hz);
    uint32_t psc = 0;
    uint32_t arr = target;

    while (arr > TIM_MAX_ARR)
    {
        psc++;
        arr = target / (psc + 1);
        if (psc > TIM_MAX_ARR) return; /* cannot achieve */
    }

    __HAL_TIM_SET_PRESCALER(&htim1, psc);
    __HAL_TIM_SET_AUTORELOAD(&htim1, arr);
}

void PWM_SetDutyCycle(uint8_t phase, float duty_percent)
{
    if (phase > 2) return;
    if (duty_percent < 0.0f) duty_percent = 0.0f;
    if (duty_percent > 100.0f) duty_percent = 100.0f;

    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
    uint32_t pulse = (uint32_t)((duty_percent * (float)arr) / 100.0f);

    uint32_t channel = PWM_PhaseToChannel(phase);
    __HAL_TIM_SET_COMPARE(&htim1, channel, pulse);
}

void PWM_StartPhase(uint8_t phase)
{
    if (phase > 2) return;
    uint32_t channel = PWM_PhaseToChannel(phase);
    HAL_TIM_PWM_Start(&htim1, channel);
    HAL_TIMEx_PWMN_Start(&htim1, channel);
}

void PWM_StopPhase(uint8_t phase)
{
    if (phase > 2) return;
    uint32_t channel = PWM_PhaseToChannel(phase);
    HAL_TIM_PWM_Stop(&htim1, channel);
    HAL_TIMEx_PWMN_Stop(&htim1, channel);
}

void PWM_Start(void)
{
    PWM_StartPhase(0);
    PWM_StartPhase(1);
    PWM_StartPhase(2);
}

void PWM_Stop(void)
{
    PWM_StopPhase(0);
    PWM_StopPhase(1);
    PWM_StopPhase(2);
}

void PWM_ClearFault(void)
{
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK);
    __HAL_TIM_MOE_ENABLE(&htim1);
}

/* USER CODE END 1 */

