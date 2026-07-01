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

/**
 * @brief Set a static three-phase voltage vector at the given electrical angle.
 *
 * The angle follows the same convention as the running SPWM ISR, with the
 * phase-U peak at +90 deg.  Calling this does NOT start the rotating SPWM ramp;
 * it simply parks the PWM outputs at the requested vector.  Useful for rotor
 * alignment during calibration.
 */
void PWM_SetVoltageAngle(float angle_rad, float modulation_index);

void PWM_StartSPWM(float fundamental_freq_hz, float modulation_index);
void PWM_StopSPWM(void);
void PWM_SetSPWMParams(float fundamental_freq_hz, float modulation_index);

uint32_t PWM_GetSPWMElectricalCycles(void);
void PWM_ResetSPWMElectricalCycles(void);

/**
 * @brief Current electrical angle of the running SPWM ramp, in radians.
 *
 * Wraps every 2*pi.  Returns 0 if SPWM is not running.
 */
float PWM_GetSPWMAngle(void);

void PWM_StartPhase(uint8_t phase);
void PWM_StopPhase(uint8_t phase);
void PWM_Start(void);
void PWM_Stop(void);
void PWM_ClearFault(void);
void PWM_ClearBreakFlag(void);

void PWM_PrintState(void);
void PWM_PrintSPWMState(void);

#ifdef __cplusplus
}
#endif

#endif /* __INVERTER_PWM_H__ */
