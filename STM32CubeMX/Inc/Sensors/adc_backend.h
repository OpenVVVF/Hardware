/**
  ******************************************************************************
  * @file    adc_backend.h
  * @brief   STM32 ADC backend for the measurement system.
  *          Reads named channels and returns raw voltages.
  ******************************************************************************
  */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"
#include "Drivers/max22530_driver.h"

/**
 * @brief  Initialise the ADC backends (internal ADCs + isolated SPI ADC).
 * @return HAL_OK on success.
 */
HAL_StatusTypeDef AdcBackend_Init(void);

/**
 * @brief  Access the isolated DC-link voltage ADC handle.
 */
MAX22530_HandleTypeDef *AdcBackend_GetVdcAdc(void);

/**
 * @brief  Read a named channel and return its voltage in volts.
 * @param  name  Channel name (e.g. "V_DC_BUS", "ENCODER_SIN", "MOTOR_TEMP").
 * @return Voltage in volts, or 0.0f if the channel is unknown.
 */
float AdcBackend_ReadChannelVoltage(const char *name);

#ifdef __cplusplus
}
#endif
