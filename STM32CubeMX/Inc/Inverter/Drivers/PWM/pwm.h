#ifndef __INVERTER_PWM_H__
#define __INVERTER_PWM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PWM_DEFAULT_SWITCHING_FREQ_HZ   1000U
#define PWM_DEFAULT_DEADTIME_NS         1000U   /* 1 us */

void PWM_SetFrequency(uint32_t freq_hz);
void PWM_SetDeadTime(uint32_t deadtime_ns);
void PWM_SetDutyCycle(uint8_t phase, float duty_percent);
void PWM_SetThreePhaseDuty(float duty_u, float duty_v, float duty_w);

void PWM_StartSPWM(float fundamental_freq_hz, float modulation_index);
void PWM_StopSPWM(void);
void PWM_SetSPWMParams(float fundamental_freq_hz, float modulation_index);

uint32_t PWM_GetSPWMElectricalCycles(void);
void PWM_ResetSPWMElectricalCycles(void);

void PWM_StartPhase(uint8_t phase);
void PWM_StopPhase(uint8_t phase);
void PWM_Start(void);
void PWM_Stop(void);
void PWM_ClearFault(void);

void PWM_PrintState(void);
void PWM_PrintSPWMState(void);

#ifdef __cplusplus
}
#endif

#endif /* __INVERTER_PWM_H__ */
