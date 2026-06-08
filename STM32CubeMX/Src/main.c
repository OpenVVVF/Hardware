/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "fdcan.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "mcp2221a_driver.h"
#include "cy15b102q_driver.h"
#include "ontime_logger.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART3_UART_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_ADC3_Init();
  MX_FDCAN1_Init();
  MX_FDCAN2_Init();
  MX_SPI4_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_TogglePin(DEBUG_GREEN_LED_GPIO_Port, DEBUG_GREEN_LED_Pin);
  HAL_Delay(1000);
  HAL_GPIO_TogglePin(CANBUS_POWER_ENABLE_GPIO_Port, CANBUS_POWER_ENABLE_Pin);
  HAL_GPIO_TogglePin(GATE_DRIVER_POWER_ENABLE_GPIO_Port, GATE_DRIVER_POWER_ENABLE_Pin);
  HAL_GPIO_TogglePin(PERIPHERAL_POWER_ENABLE_GPIO_Port, PERIPHERAL_POWER_ENABLE_Pin);
  HAL_GPIO_TogglePin(USER_DOUT_1_GPIO_Port, USER_DOUT_1_Pin);
  HAL_GPIO_TogglePin(USER_DOUT_2_GPIO_Port, USER_DOUT_2_Pin);
  HAL_GPIO_TogglePin(USER_DOUT_3_GPIO_Port, USER_DOUT_3_Pin);
  HAL_GPIO_TogglePin(USER_DOUT_4_GPIO_Port, USER_DOUT_4_Pin);
  MCP2221A_Init(&huart3);

  /* ---------- CY15B102Q F-RAM init & sanity test ---------- */
  CY15B102Q_HandleTypeDef fram = {
      .hspi      = &hspi4,
      .cs_port   = FRAM_CS_GPIO_Port,
      .cs_pin    = FRAM_CS_Pin,
      .wp_port   = FRAM_WP_GPIO_Port,
      .wp_pin    = FRAM_WP_Pin,
      .hold_port = FRAM_HOLD_GPIO_Port,
      .hold_pin  = FRAM_HOLD_Pin,
  };

  if (CY15B102Q_Init(&fram) != HAL_OK)
  {
      MCP2221A_PrintLn("[F-RAM] ERROR: Device ID mismatch / not detected");
  }
  else
  {
      uint8_t status = CY15B102Q_ReadStatus(&fram);
      MCP2221A_Printf("[F-RAM] Status=0x%02X\r\n", status);

      uint32_t test_addr = 0x01000U;   /* keep clear of on-time record at 0x00000 */
      uint8_t tx_buf[16] = {0xDE,0xAD,0xBE,0xEF,0xCA,0xFE,0xBA,0xBE,
                            0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
      uint8_t rx_buf[16] = {0};

      CY15B102Q_Write(&fram, test_addr, tx_buf, 16);
      CY15B102Q_Read(&fram, test_addr, rx_buf, 16);

      bool ok = true;
      for (int i = 0; i < 16; i++)
      {
          if (rx_buf[i] != tx_buf[i]) ok = false;
      }

      if (ok)
      {
          MCP2221A_PrintLn("[F-RAM] WRITE + READ test PASSED");
      }
      else
      {
          MCP2221A_PrintLn("[F-RAM] WRITE + READ test FAILED");
      }

      /* ---------- Persistent on-time logger ---------- */
      bool ontime_valid = OnTime_Init(&fram);
      char ontime_str[64];
      OnTime_Format(ontime_str, sizeof(ontime_str));
      MCP2221A_Printf("[ONTIME] Boot #%lu | Previous total: %s | Valid=%s\r\n",
                       (unsigned long)OnTime_GetBootCount(),
                       ontime_str,
                       ontime_valid ? "YES" : "NO (fresh start)");
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_GPIO_TogglePin(DEBUG_GREEN_LED_GPIO_Port, DEBUG_GREEN_LED_Pin);
//    HAL_GPIO_TogglePin(DEBUG_ORANGE_LED_GPIO_Port, DEBUG_ORANGE_LED_Pin);
    HAL_Delay(1000);

    OnTime_Update();

    char ontime_str[64];
    OnTime_Format(ontime_str, sizeof(ontime_str));
    MCP2221A_Printf("[SEXUPDATE V4] Total on-time: %s | Boot #%lu | Session tick=%lu ms\r\n",
                     ontime_str,
                     (unsigned long)OnTime_GetBootCount(),
                     (unsigned long)HAL_GetTick());
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 68;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 6144;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_FDCAN;
  PeriphClkInitStruct.PLL2.PLL2M = 1;
  PeriphClkInitStruct.PLL2.PLL2N = 24;
  PeriphClkInitStruct.PLL2.PLL2P = 2;
  PeriphClkInitStruct.PLL2.PLL2Q = 2;
  PeriphClkInitStruct.PLL2.PLL2R = 2;
  PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_3;
  PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
  PeriphClkInitStruct.FdcanClockSelection = RCC_FDCANCLKSOURCE_PLL2;
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
