/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    pwm.c
  * @brief   Three-phase PWM / SPWM control for TIM1 gate-driver output.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "pwm.h"
#include "tim.h"
#include "mcp2221a_driver.h"
#include <math.h>

/* USER CODE BEGIN 0 */
#define TIM1_CLOCK_HZ       275000000UL
#define TIM_MAX_ARR         65535U
#define TWO_PI              6.283185307f

/* Phase-to-timer-channel mapping.
 * Index 0 = phase U, 1 = phase V, 2 = phase W.
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
