/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define FRAM_SPI_SCK_Pin GPIO_PIN_2
#define FRAM_SPI_SCK_GPIO_Port GPIOE
#define DEBUG_GREEN_LED_Pin GPIO_PIN_3
#define DEBUG_GREEN_LED_GPIO_Port GPIOE
#define FRAM_SPI_MISO_Pin GPIO_PIN_5
#define FRAM_SPI_MISO_GPIO_Port GPIOE
#define FRAM_SPI_MOSI_Pin GPIO_PIN_6
#define FRAM_SPI_MOSI_GPIO_Port GPIOE
#define FRAM_WP_Pin GPIO_PIN_13
#define FRAM_WP_GPIO_Port GPIOC
#define FRAM_CS_Pin GPIO_PIN_14
#define FRAM_CS_GPIO_Port GPIOC
#define FRAM_HOLD_Pin GPIO_PIN_15
#define FRAM_HOLD_GPIO_Port GPIOC
#define PERIPHERAL_POWER_ENABLE_Pin GPIO_PIN_2
#define PERIPHERAL_POWER_ENABLE_GPIO_Port GPIOF
#define AIN_MOTOR_TMP_Pin GPIO_PIN_4
#define AIN_MOTOR_TMP_GPIO_Port GPIOF
#define AIN_ENCODER_SIN_HALL_U_Pin GPIO_PIN_0
#define AIN_ENCODER_SIN_HALL_U_GPIO_Port GPIOC
#define AIN_ENCODER_COS_HALL_V_Pin GPIO_PIN_1
#define AIN_ENCODER_COS_HALL_V_GPIO_Port GPIOC
#define AIN_PH_W_CURSENS_REF_Pin GPIO_PIN_2
#define AIN_PH_W_CURSENS_REF_GPIO_Port GPIOC
#define AIN_PH_W_CURSENS_Pin GPIO_PIN_3
#define AIN_PH_W_CURSENS_GPIO_Port GPIOC
#define AIN_TMP_SENSE_3_Pin GPIO_PIN_0
#define AIN_TMP_SENSE_3_GPIO_Port GPIOA
#define AIN_TMP_SENSE_2_Pin GPIO_PIN_1
#define AIN_TMP_SENSE_2_GPIO_Port GPIOA
#define AIN_HALL_W_Pin GPIO_PIN_2
#define AIN_HALL_W_GPIO_Port GPIOA
#define AIN_THROTTLE_A_Pin GPIO_PIN_3
#define AIN_THROTTLE_A_GPIO_Port GPIOA
#define AIN_THROTTLE_B_Pin GPIO_PIN_4
#define AIN_THROTTLE_B_GPIO_Port GPIOA
#define AIN_TMP_SENSE_1_Pin GPIO_PIN_5
#define AIN_TMP_SENSE_1_GPIO_Port GPIOA
#define AIN_PH_V_CURSENS_Pin GPIO_PIN_6
#define AIN_PH_V_CURSENS_GPIO_Port GPIOA
#define AIN_PH_V_CURSENS_REF_Pin GPIO_PIN_7
#define AIN_PH_V_CURSENS_REF_GPIO_Port GPIOA
#define AIN_PH_U_CURSENS_Pin GPIO_PIN_4
#define AIN_PH_U_CURSENS_GPIO_Port GPIOC
#define AIN_PH_U_CURSENS_REF_Pin GPIO_PIN_5
#define AIN_PH_U_CURSENS_REF_GPIO_Port GPIOC
#define AIN_PH_W_CURSENS_REFB0_Pin GPIO_PIN_0
#define AIN_PH_W_CURSENS_REFB0_GPIO_Port GPIOB
#define AIN_PH_W_CURSENSB1_Pin GPIO_PIN_1
#define AIN_PH_W_CURSENSB1_GPIO_Port GPIOB
#define AIN_DC_LINK_CURSENS_Pin GPIO_PIN_11
#define AIN_DC_LINK_CURSENS_GPIO_Port GPIOF
#define AIN_DC_LINK_CURSENS_REF_Pin GPIO_PIN_12
#define AIN_DC_LINK_CURSENS_REF_GPIO_Port GPIOF
#define AIN_PH_W_CURSENSF13_Pin GPIO_PIN_13
#define AIN_PH_W_CURSENSF13_GPIO_Port GPIOF
#define AIN_PH_W_CURSENS_REFF14_Pin GPIO_PIN_14
#define AIN_PH_W_CURSENS_REFF14_GPIO_Port GPIOF
#define PH_U_HIGH_Pin GPIO_PIN_8
#define PH_U_HIGH_GPIO_Port GPIOE
#define PH_U_LOW_Pin GPIO_PIN_9
#define PH_U_LOW_GPIO_Port GPIOE
#define PH_V_HIGH_Pin GPIO_PIN_10
#define PH_V_HIGH_GPIO_Port GPIOE
#define PH_V_LOW_Pin GPIO_PIN_11
#define PH_V_LOW_GPIO_Port GPIOE
#define PH_W_HIGH_Pin GPIO_PIN_12
#define PH_W_HIGH_GPIO_Port GPIOE
#define PH_W_LOW_Pin GPIO_PIN_13
#define PH_W_LOW_GPIO_Port GPIOE
#define GATE_DRIVER_FAULT_PWM_BREAK_Pin GPIO_PIN_15
#define GATE_DRIVER_FAULT_PWM_BREAK_GPIO_Port GPIOE
#define USER_DIN_6_Pin GPIO_PIN_15
#define USER_DIN_6_GPIO_Port GPIOD
#define USER_DIN_5_Pin GPIO_PIN_2
#define USER_DIN_5_GPIO_Port GPIOG
#define USER_DIN_4_Pin GPIO_PIN_3
#define USER_DIN_4_GPIO_Port GPIOG
#define USER_DIN_3_Pin GPIO_PIN_4
#define USER_DIN_3_GPIO_Port GPIOG
#define USER_DIN_2_Pin GPIO_PIN_5
#define USER_DIN_2_GPIO_Port GPIOG
#define USER_DIN_1_Pin GPIO_PIN_6
#define USER_DIN_1_GPIO_Port GPIOG
#define USER_DIN_7_Pin GPIO_PIN_7
#define USER_DIN_7_GPIO_Port GPIOG
#define USER_DIN_8_Pin GPIO_PIN_8
#define USER_DIN_8_GPIO_Port GPIOG
#define CANBUS_POWER_ENABLE_Pin GPIO_PIN_8
#define CANBUS_POWER_ENABLE_GPIO_Port GPIOC
#define GATE_DRIVER_POWER_ENABLE_Pin GPIO_PIN_10
#define GATE_DRIVER_POWER_ENABLE_GPIO_Port GPIOC
#define GATE_DRIVER_FAULT_Pin GPIO_PIN_11
#define GATE_DRIVER_FAULT_GPIO_Port GPIOC
#define GATE_DRIVER_READY_Pin GPIO_PIN_12
#define GATE_DRIVER_READY_GPIO_Port GPIOC
#define VSENSE_ISO_ADC_INTERRUPT_Pin GPIO_PIN_1
#define VSENSE_ISO_ADC_INTERRUPT_GPIO_Port GPIOD
#define SPI2_CS_Pin GPIO_PIN_2
#define SPI2_CS_GPIO_Port GPIOD
#define GATE_DRIVER_RESET_Pin GPIO_PIN_5
#define GATE_DRIVER_RESET_GPIO_Port GPIOD
#define USER_DOUT_4_Pin GPIO_PIN_11
#define USER_DOUT_4_GPIO_Port GPIOG
#define USER_DOUT_3_Pin GPIO_PIN_12
#define USER_DOUT_3_GPIO_Port GPIOG
#define USER_DOUT_2_Pin GPIO_PIN_13
#define USER_DOUT_2_GPIO_Port GPIOG
#define USER_DOUT_1_Pin GPIO_PIN_14
#define USER_DOUT_1_GPIO_Port GPIOG
#define DEBUG_ORANGE_LED_Pin GPIO_PIN_1
#define DEBUG_ORANGE_LED_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
