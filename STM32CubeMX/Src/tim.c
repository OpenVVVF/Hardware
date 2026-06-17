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
#include "mcp2221a_driver.h"
#include "melody.h"

/* USER CODE BEGIN 0 */
#define TIM1_CLOCK_HZ       275000000UL
#define TIM_MAX_ARR         65535U
#define TWO_PI              6.283185307f
#define MELODY_REST_PWM_HZ  8000U

/* Phase-to-timer-channel mapping.
 * Index 0 = phase U, 1 = phase V, 2 = phase W.
 * If your board wiring has the phases in a different order on the scope,
 * change this table (e.g. swap channels) instead of rewiring.
 *
 * Current hardware mapping on STM32H723ZG:
 *   TIM1_CH1  / PE9  -> PH_U_LOW  (main channel)
 *   TIM1_CH1N / PE8  -> PH_U_HIGH (complementary channel)
 *   TIM1_CH2  / PE11 -> PH_V_LOW  (main channel)
 *   TIM1_CH2N / PE10 -> PH_V_HIGH (complementary channel)
 *   TIM1_CH3  / PE13 -> PH_W_LOW  (main channel)
 *   TIM1_CH3N / PE12 -> PH_W_HIGH (complementary channel)
 */
static const uint32_t pwm_phase_channels[3] = {
    TIM_CHANNEL_1,
    TIM_CHANNEL_2,
    TIM_CHANNEL_3
};

/* Current switching frequency, updated by PWM_SetFrequency. */
static volatile float pwm_switching_freq_hz = (float)PWM_DEFAULT_SWITCHING_FREQ_HZ;

/* SPWM state, updated in the TIM1 update ISR. */
static volatile float spwm_angle = 0.0f;
static volatile float spwm_fundamental_freq_hz = 1.0f;
static volatile float spwm_modulation_index = 0.0;
static volatile uint8_t spwm_running = 0;

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
    if (phase > 2) return 0;
    return pwm_phase_channels[phase];
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
  /* Center-aligned: f_pwm = f_tim / (2 * (PSC+1) * ARR)
     Default 5 kHz -> ARR = 275 MHz / (2 * 5 kHz) = 27500 */
  htim1.Init.Period = 27500;
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
  sBreakDeadTimeConfig.DeadTime = PWM_ComputeDeadTime(PWM_DEFAULT_DEADTIME_NS);
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

    pwm_switching_freq_hz = (float)TIM1_CLOCK_HZ /
                            (2.0f * (float)(arr + 1U) * (float)(psc + 1U));
}

void PWM_SetDeadTime(uint32_t deadtime_ns)
{
    /* Update DTG field in BDTR while preserving break/dead-time configuration.
       MOE should be disabled before changing deadtime if PWM is running. */
    uint32_t dtg = (uint32_t)PWM_ComputeDeadTime(deadtime_ns);
    uint32_t bdtr = TIM1->BDTR;
    bdtr &= ~TIM_BDTR_DTG;
    bdtr |= dtg;
    TIM1->BDTR = bdtr;
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

void PWM_SetThreePhaseDuty(float duty_u, float duty_v, float duty_w)
{
    PWM_SetDutyCycle(0, duty_u);
    PWM_SetDutyCycle(1, duty_v);
    PWM_SetDutyCycle(2, duty_w);
}

void PWM_StartSPWM(float fundamental_freq_hz, float modulation_index)
{
    if (fundamental_freq_hz < 0.0f) fundamental_freq_hz = 0.0f;
    if (modulation_index < 0.0f) modulation_index = 0.0f;
    if (modulation_index > 1.0f) modulation_index = 1.0f;

    spwm_fundamental_freq_hz = fundamental_freq_hz;
    spwm_modulation_index = modulation_index;
    spwm_angle = 0.0f;
    spwm_running = 1;

    HAL_NVIC_SetPriority(TIM1_UP_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
    __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
}

void PWM_StopSPWM(void)
{
    spwm_running = 0;
    __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
    HAL_NVIC_DisableIRQ(TIM1_UP_IRQn);
}

void PWM_SetSPWMParams(float fundamental_freq_hz, float modulation_index)
{
    if (fundamental_freq_hz < 0.0f) fundamental_freq_hz = 0.0f;
    if (modulation_index < 0.0f) modulation_index = 0.0f;
    if (modulation_index > 1.0f) modulation_index = 1.0f;

    spwm_fundamental_freq_hz = fundamental_freq_hz;
    spwm_modulation_index = modulation_index;
}

/* TIM1 update ISR callback. Runs at the PWM switching frequency. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM1 || !spwm_running) return;

    /* Update melody timing ~once per millisecond, independent of PWM carrier. */
    static float melody_ms_accum = 0.0f;
    melody_ms_accum += 1000.0f / pwm_switching_freq_hz;
    if (melody_ms_accum >= 1.0f)
    {
        melody_ms_accum -= 1.0f;
        Melody_Update(1);
    }

    /* Apply current melody (or manual) note to the PWM carrier.
       PWM carrier = note * 0.5 so the motor emits the note frequency. */
    static float last_note_hz = -1.0f;
    float note_hz = Melody_GetCurrentNoteHz();
    if (note_hz != last_note_hz)
    {
        last_note_hz = note_hz;
        if (note_hz > 0.0f)
        {
            PWM_SetFrequency((uint32_t)(note_hz * 0.5f + 0.5f));
        }
        else
        {
            /* REST: high-frequency idle carrier so the gap is not a low/obnoxious tone. */
            PWM_SetFrequency(MELODY_REST_PWM_HZ);
        }
    }

    float angle = spwm_angle;
    float m = spwm_modulation_index;

    /* Three-phase sinusoidal duty cycles, centred at 50 %. */
    float u = 50.0f + 50.0f * m * sinf(angle);
    float v = 50.0f + 50.0f * m * sinf(angle - TWO_PI / 3.0f);
    float w = 50.0f + 50.0f * m * sinf(angle + TWO_PI / 3.0f);

    PWM_SetThreePhaseDuty(u, v, w);

    /* Advance angle by one PWM period. */
    angle += TWO_PI * spwm_fundamental_freq_hz / pwm_switching_freq_hz;
    if (angle >= TWO_PI) angle -= TWO_PI;
    spwm_angle = angle;
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

void PWM_PrintState(void)
{
    uint32_t bdtr = TIM1->BDTR;
    uint32_t sr   = TIM1->SR;
    uint32_t ccer = TIM1->CCER;

    MCP2221A_Printf("[PWM] BDTR=0x%04lX | MOE=%lu | BKP=%lu | DTG=0x%02lX\r\n",
                     bdtr, (bdtr >> 15) & 1, (bdtr >> 13) & 1, bdtr & TIM_BDTR_DTG);
    MCP2221A_Printf("[PWM] SR=0x%04lX | BIF=%lu | BKF=%lu\r\n",
                     sr, (sr >> 7) & 1, (sr >> 6) & 1);
    MCP2221A_Printf("[PWM] CCER=0x%04lX | CH1E=%lu CH1NE=%lu | CH2E=%lu CH2NE=%lu | CH3E=%lu CH3NE=%lu\r\n",
                     ccer,
                     (ccer >> 0) & 1, (ccer >> 2) & 1,
                     (ccer >> 4) & 1, (ccer >> 6) & 1,
                     (ccer >> 8) & 1, (ccer >> 10) & 1);
}

void PWM_PrintSPWMState(void)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
    float du = (arr == 0) ? 0.0f : (__HAL_TIM_GET_COMPARE(&htim1, pwm_phase_channels[0]) * 100.0f / (float)arr);
    float dv = (arr == 0) ? 0.0f : (__HAL_TIM_GET_COMPARE(&htim1, pwm_phase_channels[1]) * 100.0f / (float)arr);
    float dw = (arr == 0) ? 0.0f : (__HAL_TIM_GET_COMPARE(&htim1, pwm_phase_channels[2]) * 100.0f / (float)arr);

    MCP2221A_Printf("[SPWM] running=%u f=%.2f Hz m=%.3f | duties U=%.1f V=%.1f W=%.1f %%\r\n",
                     (unsigned)spwm_running,
                     (double)spwm_fundamental_freq_hz,
                     (double)spwm_modulation_index,
                     (double)du, (double)dv, (double)dw);
}

/* USER CODE END 1 */

