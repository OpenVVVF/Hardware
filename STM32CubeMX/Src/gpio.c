/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
     PB3(JTDO/TRACESWO)   ------> SPI1_SCK
     PB4(NJTRST)   ------> SPI1_MISO
     PB5   ------> SPI1_MOSI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, DEBUG_GREEN_LED_Pin|DEBUG_ORANGE_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, FRAM_WP_Pin|FRAM_CS_Pin|FRAM_HOLD_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(PERIPHERAL_POWER_ENABLE_GPIO_Port, PERIPHERAL_POWER_ENABLE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, CANBUS_POWER_ENABLE_Pin|GATE_DRIVER_POWER_ENABLE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, SPI2_CS_Pin|GATE_DRIVER_RESET_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, USER_DOUT_4_Pin|USER_DOUT_3_Pin|USER_DOUT_2_Pin|USER_DOUT_1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : DEBUG_GREEN_LED_Pin DEBUG_ORANGE_LED_Pin */
  GPIO_InitStruct.Pin = DEBUG_GREEN_LED_Pin|DEBUG_ORANGE_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : FRAM_WP_Pin FRAM_CS_Pin FRAM_HOLD_Pin CANBUS_POWER_ENABLE_Pin
                           GATE_DRIVER_POWER_ENABLE_Pin */
  GPIO_InitStruct.Pin = FRAM_WP_Pin|FRAM_CS_Pin|FRAM_HOLD_Pin|CANBUS_POWER_ENABLE_Pin
                          |GATE_DRIVER_POWER_ENABLE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PERIPHERAL_POWER_ENABLE_Pin */
  GPIO_InitStruct.Pin = PERIPHERAL_POWER_ENABLE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PERIPHERAL_POWER_ENABLE_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : USER_DIN_6_Pin VSENSE_ISO_ADC_INTERRUPT_Pin */
  GPIO_InitStruct.Pin = USER_DIN_6_Pin|VSENSE_ISO_ADC_INTERRUPT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : USER_DIN_5_Pin USER_DIN_4_Pin USER_DIN_3_Pin USER_DIN_2_Pin
                           USER_DIN_1_Pin USER_DIN_7_Pin USER_DIN_8_Pin */
  GPIO_InitStruct.Pin = USER_DIN_5_Pin|USER_DIN_4_Pin|USER_DIN_3_Pin|USER_DIN_2_Pin
                          |USER_DIN_1_Pin|USER_DIN_7_Pin|USER_DIN_8_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : PA15 */
  GPIO_InitStruct.Pin = GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : GATE_DRIVER_FAULT_Pin GATE_DRIVER_READY_Pin */
  GPIO_InitStruct.Pin = GATE_DRIVER_FAULT_Pin|GATE_DRIVER_READY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : SPI2_CS_Pin GATE_DRIVER_RESET_Pin */
  GPIO_InitStruct.Pin = SPI2_CS_Pin|GATE_DRIVER_RESET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : USER_DOUT_4_Pin USER_DOUT_3_Pin USER_DOUT_2_Pin USER_DOUT_1_Pin */
  GPIO_InitStruct.Pin = USER_DOUT_4_Pin|USER_DOUT_3_Pin|USER_DOUT_2_Pin|USER_DOUT_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pins : PB3 PB4 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
