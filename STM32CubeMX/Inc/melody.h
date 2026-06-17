/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    melody.h
  * @brief   Simple melody sequencer for PWM-driven "buzzer" output.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MELODY_H__
#define __MELODY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void Melody_Start(void);
void Melody_Stop(void);
void Melody_SetManualNote(float note_hz);
void Melody_Update(uint16_t ms_elapsed);
float Melody_GetCurrentNoteHz(void);
uint8_t Melody_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* __MELODY_H__ */
