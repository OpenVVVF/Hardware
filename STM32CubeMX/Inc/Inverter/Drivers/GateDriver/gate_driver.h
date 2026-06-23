/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gate_driver.h
  * @brief   Gate driver control interface for NCD57100DWR2G
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __GATE_DRIVER_H__
#define __GATE_DRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdbool.h>

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void GateDriver_Init(void);
void GateDriver_ResetPulse(void);
void GateDriver_EnablePower(bool enable);
void GateDriver_EnableOutputs(void);
void GateDriver_DisableOutputs(void);
bool GateDriver_IsFault(void);
bool GateDriver_IsReady(void);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __GATE_DRIVER_H__ */
