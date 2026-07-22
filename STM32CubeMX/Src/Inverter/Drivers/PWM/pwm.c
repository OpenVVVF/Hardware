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
#include <stdbool.h>

/* Forward declaration for the FOC ISR hook (defined in FocControlManager.cpp). */
extern void FocControlManager_OnPwmPeriod(void);

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

/* Current TIM1 update frequency.  For center-aligned PWM with RCR=1 this equals
 * the switching frequency (used for both SPWM and FOC so the control loop stays
 * synchronized with the once-per-period ADC trigger). */
static volatile float pwm_update_freq_hz = (float)PWM_DEFAULT_SWITCHING_FREQ_HZ;

/* SPWM state, updated in the TIM1 update ISR. */
static volatile float spwm_angle = 0.0f;
static volatile float spwm_fundamental_freq_hz = 1.0f;
static volatile float spwm_modulation_index = 0.0;
static volatile uint8_t spwm_running = 0;
static volatile uint32_t spwm_elec_cycles = 0;

/* FOC mode flag. When set, the TIM1 update ISR does not run the SPWM ramp. */
static volatile uint8_t foc_active = 0;

/* CKD = DIV1  =>  t_DTS = 1 / TIM1_CLOCK_HZ.
 * Map the requested deadtime to the finest STM32 DTG encoding. */
static uint8_t PWM_ComputeDeadTime(uint32_t deadtime_ns)
{
    const uint32_t ticks = (uint32_t)((deadtime_ns * (uint64_t)TIM1_CLOCK_HZ +
                                       500000000ULL) / 1000000000ULL);

    if (ticks <= 127U) {
        /* 0xx: DT = DTG[7:0] * t_DTS */
        return (uint8_t)ticks;
    } else if (ticks <= 254U) {
        /* 10x: DT = (64 + DTG[5:0]) * 2 * t_DTS */
        uint32_t dtg = (ticks / 2U) - 64U;
        if (dtg > 63U) dtg = 63U;
        return (uint8_t)(0x80U | dtg);
    } else if (ticks <= 736U) {
        /* 110: DT = (32 + DTG[4:0] * 2) * 8 * t_DTS */
        uint32_t dtg = (ticks / 8U - 32U + 1U) / 2U;
        if (dtg > 31U) dtg = 31U;
        return (uint8_t)(0xC0U | dtg);
    } else {
        /* 111: DT = (32 + DTG[4:0]) * 16 * t_DTS */
        uint32_t dtg = (ticks / 16U) - 32U;
        if (dtg > 31U) dtg = 31U;
        return (uint8_t)(0xE0U | dtg);
    }
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

    /* Default to one update event per switching period (RCR=1), matching the
     * open-loop SPWM convention.  FOC mode will override this when enabled. */
    TIM1->RCR = 1U;
    pwm_update_freq_hz = pwm_switching_freq_hz;
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

/* SVPWM linear over-modulation limit: 2/sqrt(3) */
#define SVPWM_M_MAX 1.154700538f

void PWM_SetThreePhaseDuty(float duty_u, float duty_v, float duty_w)
{
    PWM_SetDutyCycle(0, duty_u);
    PWM_SetDutyCycle(1, duty_v);
    PWM_SetDutyCycle(2, duty_w);
}

void PWM_SetVoltageAngle(float angle_rad, float modulation_index)
{
    if (modulation_index < 0.0f) modulation_index = 0.0f;
    if (modulation_index > SVPWM_M_MAX) modulation_index = SVPWM_M_MAX;

    float u = modulation_index * sinf(angle_rad);
    float v = modulation_index * sinf(angle_rad - TWO_PI / 3.0f);
    float w = modulation_index * sinf(angle_rad + TWO_PI / 3.0f);

    /* Min-max SVPWM zero-sequence injection. */
    float v_max = (u > v) ? ((u > w) ? u : w) : ((v > w) ? v : w);
    float v_min = (u < v) ? ((u < w) ? u : w) : ((v < w) ? v : w);
    float v0 = -0.5f * (v_max + v_min);

    float du = 50.0f + 50.0f * (u + v0);
    float dv = 50.0f + 50.0f * (v + v0);
    float dw = 50.0f + 50.0f * (w + v0);

    if (du < 0.0f) du = 0.0f; else if (du > 100.0f) du = 100.0f;
    if (dv < 0.0f) dv = 0.0f; else if (dv > 100.0f) dv = 100.0f;
    if (dw < 0.0f) dw = 0.0f; else if (dw > 100.0f) dw = 100.0f;

    PWM_SetThreePhaseDuty(du, dv, dw);
}

void PWM_SetVoltageVector(float valpha_v, float vbeta_v, float vdc_v)
{
    if (vdc_v <= 1.0f) {
        PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
        return;
    }

    /* Clamp the alpha/beta magnitude to the linear modulation limit before
     * converting to three-phase voltages.  The SVPWM linear limit is
     * Vdc / sqrt(3). */
    const float sqrt3 = 1.7320508075688772f;
    float valpha = valpha_v;
    float vbeta  = vbeta_v;
    const float v_max_linear = (vdc_v / sqrt3) * 0.95f;
    const float v_albe_sq = valpha * valpha + vbeta * vbeta;
    if (v_albe_sq > v_max_linear * v_max_linear && v_albe_sq > 1e-12f) {
        const float scale = v_max_linear / sqrtf(v_albe_sq);
        valpha *= scale;
        vbeta  *= scale;
    }

    /* Inverse Clarke: alpha/beta -> A/B/C. */
    float va = valpha;
    float vb = -0.5f * valpha + 0.5f * sqrt3 * vbeta;
    float vc = -0.5f * valpha - 0.5f * sqrt3 * vbeta;

    /* Min-max SVPWM zero-sequence injection. */
    float v_max = (va > vb) ? ((va > vc) ? va : vc) : ((vb > vc) ? vb : vc);
    float v_min = (va < vb) ? ((va < vc) ? va : vc) : ((vb < vc) ? vb : vc);
    float vcom = 0.5f * (v_max + v_min);

    float du = 50.0f + 50.0f * (va - vcom) / vdc_v;
    float dv = 50.0f + 50.0f * (vb - vcom) / vdc_v;
    float dw = 50.0f + 50.0f * (vc - vcom) / vdc_v;

    if (du < 0.0f) du = 0.0f; else if (du > 100.0f) du = 100.0f;
    if (dv < 0.0f) dv = 0.0f; else if (dv > 100.0f) dv = 100.0f;
    if (dw < 0.0f) dw = 0.0f; else if (dw > 100.0f) dw = 100.0f;

    PWM_SetThreePhaseDuty(du, dv, dw);
}

void PWM_EnableFocMode(void)
{
    foc_active = 1;
    /* Single-update FOC: run the control ISR once per PWM switching period,
     * matching the ADC sample rate.  The phase-current ADC is triggered once
     * per period by TIM1 CH4 / OC4REF, so the FOC loop must consume data at
     * the same rate.  RCR=1 generates one update event per PWM period. */
    TIM1->RCR = 1U;
    pwm_update_freq_hz = pwm_switching_freq_hz;
}

void PWM_DisableFocMode(void)
{
    foc_active = 0;
    TIM1->RCR = 1U;
    pwm_update_freq_hz = pwm_switching_freq_hz;
}

bool PWM_IsFocModeActive(void)
{
    return foc_active != 0;
}

void PWM_StartUpdateInterrupt(void)
{
    HAL_NVIC_SetPriority(TIM1_UP_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
    __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
}

void PWM_StopUpdateInterrupt(void)
{
    if (!spwm_running) {
        __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
        HAL_NVIC_DisableIRQ(TIM1_UP_IRQn);
    }
}

float PWM_GetFrequency(void)
{
    return pwm_switching_freq_hz;
}

float PWM_GetUpdateFrequency(void)
{
    return pwm_update_freq_hz;
}

void PWM_StartSPWM(float fundamental_freq_hz, float modulation_index)
{
    if (fundamental_freq_hz < 0.0f) fundamental_freq_hz = 0.0f;
    if (modulation_index < 0.0f) modulation_index = 0.0f;
    if (modulation_index > SVPWM_M_MAX) modulation_index = SVPWM_M_MAX;

    /* SPWM uses one update event per switching period (RCR=1) so the angle
     * advances at the switching frequency. */
    TIM1->RCR = 1U;
    pwm_update_freq_hz = pwm_switching_freq_hz;

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
    if (modulation_index > SVPWM_M_MAX) modulation_index = SVPWM_M_MAX;

    spwm_fundamental_freq_hz = fundamental_freq_hz;
    spwm_modulation_index = modulation_index;
}

/* TIM1 update ISR callback. Runs at the PWM switching frequency. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM1) return;

    if (foc_active) {
        FocControlManager_OnPwmPeriod();
        return;
    }

    if (!spwm_running) return;

    float angle = spwm_angle;
    float m = spwm_modulation_index;

    /* Three-phase sinusoidal references, 120° apart. */
    float u = m * sinf(angle);
    float v = m * sinf(angle - TWO_PI / 3.0f);
    float w = m * sinf(angle + TWO_PI / 3.0f);

    /* Min-max SVPWM zero-sequence injection to extend linear range to 2/sqrt(3). */
    float v_max = (u > v) ? ((u > w) ? u : w) : ((v > w) ? v : w);
    float v_min = (u < v) ? ((u < w) ? u : w) : ((v < w) ? v : w);
    float v0 = -0.5f * (v_max + v_min);

    /* Convert to centered duty cycles [0, 100]. */
    float du = 50.0f + 50.0f * (u + v0);
    float dv = 50.0f + 50.0f * (v + v0);
    float dw = 50.0f + 50.0f * (w + v0);

    if (du < 0.0f) du = 0.0f; else if (du > 100.0f) du = 100.0f;
    if (dv < 0.0f) dv = 0.0f; else if (dv > 100.0f) dv = 100.0f;
    if (dw < 0.0f) dw = 0.0f; else if (dw > 100.0f) dw = 100.0f;

    PWM_SetThreePhaseDuty(du, dv, dw);

    /* Advance angle by one PWM period. */
    angle += TWO_PI * spwm_fundamental_freq_hz / pwm_switching_freq_hz;
    if (angle >= TWO_PI) {
        angle -= TWO_PI;
        ++spwm_elec_cycles;
    }
    spwm_angle = angle;
}

uint32_t PWM_GetSPWMElectricalCycles(void)
{
    return spwm_elec_cycles;
}

void PWM_ResetSPWMElectricalCycles(void)
{
    spwm_elec_cycles = 0;
}

float PWM_GetSPWMAngle(void)
{
    return spwm_running ? spwm_angle : 0.0f;
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

void PWM_ClearBreakFlag(void)
{
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_BREAK);
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
