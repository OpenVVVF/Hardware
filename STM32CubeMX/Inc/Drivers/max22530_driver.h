/**
  ******************************************************************************
  * @file    max22530_driver.h
  * @brief   MAX22530 4-channel isolated 12-bit ADC driver.
  *          Datasheet: https://www.analog.com/MAX22530/datasheet
  ******************************************************************************
  */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_hal.h"

/* MAX22530 register map (7-bit address in upper 6 bits of command byte) */
#define MAX22530_REG_PROD_ID          0x00U
#define MAX22530_REG_ADC1             0x01U
#define MAX22530_REG_ADC2             0x02U
#define MAX22530_REG_ADC3             0x03U
#define MAX22530_REG_ADC4             0x04U
#define MAX22530_REG_FADC1            0x05U
#define MAX22530_REG_FADC2            0x06U
#define MAX22530_REG_FADC3            0x07U
#define MAX22530_REG_FADC4            0x08U
#define MAX22530_REG_COUTHI1          0x09U
#define MAX22530_REG_COUTHI2          0x0AU
#define MAX22530_REG_COUTHI3          0x0BU
#define MAX22530_REG_COUTHI4          0x0CU
#define MAX22530_REG_COUTLO1          0x0DU
#define MAX22530_REG_COUTLO2          0x0EU
#define MAX22530_REG_COUTLO3          0x0FU
#define MAX22530_REG_COUTLO4          0x10U
#define MAX22530_REG_COUT_STATUS      0x11U
#define MAX22530_REG_INTERRUPT_STATUS 0x12U
#define MAX22530_REG_INTERRUPT_ENABLE 0x13U
#define MAX22530_REG_CONTROL          0x14U

#define MAX22530_PRODUCT_ID           0x81U
#define MAX22530_VREF_VOLTS           1.80f
#define MAX22530_COUNTS_FULL_SCALE    4095U

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
} MAX22530_HandleTypeDef;

HAL_StatusTypeDef MAX22530_Init(MAX22530_HandleTypeDef *dev);
uint16_t          MAX22530_ReadRegister(MAX22530_HandleTypeDef *dev, uint8_t reg);
HAL_StatusTypeDef MAX22530_WriteRegister(MAX22530_HandleTypeDef *dev, uint8_t reg, uint16_t value);

uint16_t MAX22530_ReadAdcCounts(MAX22530_HandleTypeDef *dev, uint8_t channel);
uint16_t MAX22530_ReadFilteredCounts(MAX22530_HandleTypeDef *dev, uint8_t channel);
float    MAX22530_ReadVoltage(MAX22530_HandleTypeDef *dev, uint8_t channel);

#ifdef __cplusplus
}
#endif
