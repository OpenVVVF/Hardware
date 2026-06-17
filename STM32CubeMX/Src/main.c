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
#include "gate_driver.h"
#include "can_display.h"
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ADC_VREF_MV               3300
#define ADC_FULL_SCALE            65535
/* Sensor raw sensitivity = 625 mV / 600 A = 1.0417 mV/A.
   Board has a 2/3 divider (10 k + 20 k) so ADC sees 694 µV/A. */
#define SENSOR_SENSITIVITY_UV_A   694
#define ADC_BURST_COUNT           8
#define ADC_OVERSAMPLE_RATIO      16

/* Open-loop 3-phase SPWM parameters.
 * Updated every PWM period in the TIM1 update ISR for smooth rotation. */
#define SPWM_FUNDAMENTAL_FREQ_HZ  3.0f
#define SPWM_MODULATION_INDEX     0.115f
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static int32_t adc_zero_offset_counts = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */
static uint16_t ADC2_ReadChannel(uint32_t channel);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint16_t ADC2_ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_32CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    HAL_ADC_ConfigChannel(&hadc2, &sConfig);

    /* Dual-mode regular simultaneous: starting ADC1 also starts ADC2 */
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    (void)HAL_ADC_GetValue(&hadc1); /* discard ADC1 result */
    uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc2);
    return val;
}

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

  /* ---------- CAN display init (defaults match Python script) ---------- */
  DisplayState display;
  CAN_Display_GetDefaultState(&display);
  MCP2221A_PrintLn("[CAN] Display cycle starts in 2 s");

  /* ---------- Gate driver init & PWM start ---------- */
  GateDriver_Init();

  /* Default: 5 kHz switching, 1 us deadtime, all phases at 50 %
     (50 % is the idle/center point for a 3-phase inverter). */
  PWM_SetFrequency((4321)*0.5);
  PWM_SetDeadTime(PWM_DEFAULT_DEADTIME_NS);
  PWM_SetThreePhaseDuty(0.0f, 0.0f, 0.0f);
  PWM_Start();               /* Start all three complementary pairs */

  /* Start continuous 3-phase SPWM, updated every PWM period in the TIM1 ISR. */
  PWM_StartSPWM(SPWM_FUNDAMENTAL_FREQ_HZ, SPWM_MODULATION_INDEX);

  /* ---------- PWM diagnostics ---------- */
  PWM_PrintState();
  MCP2221A_Printf("[PWM] GATE_DRV FAULT=%s READY=%s\r\n",
                   GateDriver_IsFault() ? "YES" : "NO",
                   GateDriver_IsReady() ? "YES" : "NO");

  if ((TIM1->BDTR & TIM_BDTR_MOE) == 0)
  {
      MCP2221A_PrintLn("[PWM] MOE low – clearing break and re-enabling");
      PWM_ClearFault();
      PWM_PrintState();
  }

  MCP2221A_PrintLn("[PWM] Started: 5 kHz, 1 us deadtime, 3-phase SPWM running");
  MCP2221A_PrintLn("[PWM] Scope: each phase's high/low pins are complementary.");
  PWM_PrintSPWMState();

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

  /* ---------- ADC1/ADC2 calibration & dual-mode slave enable ---------- */
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK)
  {
      Error_Handler();
  }
  if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK)
  {
      Error_Handler();
  }

  /* Keep ADC2 (slave) enabled so dual-mode regular simultaneous works */
  if (HAL_ADC_Start(&hadc2) != HAL_OK)
  {
      Error_Handler();
  }

  MCP2221A_PrintLn("[ADC] ADC1/ADC2 calibrated, dual-mode regular simultaneous ready");

  /* ---------- Measure 0 A offset (assumes no current at boot) ---------- */
  int64_t offset_sum = 0;
  for (int i = 0; i < ADC_BURST_COUNT; i++)
  {
      HAL_ADC_Start(&hadc1);
      HAL_ADC_PollForConversion(&hadc1, 10);
      int32_t raw_sense = (int32_t)HAL_ADC_GetValue(&hadc1);
      int32_t raw_ref   = (int32_t)HAL_ADC_GetValue(&hadc2);
      offset_sum += (raw_ref - raw_sense); /* flipped sign to match desired polarity */
  }
  adc_zero_offset_counts = (int32_t)(offset_sum / ADC_BURST_COUNT);
  MCP2221A_Printf("[ADC] 0A offset sum = %d counts\r\n", (int)adc_zero_offset_counts);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_GPIO_TogglePin(DEBUG_GREEN_LED_GPIO_Port, DEBUG_GREEN_LED_Pin);
//    HAL_GPIO_TogglePin(DEBUG_ORANGE_LED_GPIO_Port, DEBUG_ORANGE_LED_Pin);
    HAL_Delay(100);

    OnTime_Update();

    /* ---------- CAN display: wait 2 s, then wakeup + immediate cycle ---------- */
    static bool display_woken = false;
    uint32_t now = HAL_GetTick();

    if (!display_woken && now >= 2000)
    {
        MCP2221A_PrintLn("[CAN] Pre-wakeup status:");
        CAN_Display_LogStatus();
        CAN_Display_SendWakeup();  /* replays the exact custom.csv log */
        display_woken = true;
        MCP2221A_PrintLn("[CAN] Log replay complete");
        CAN_Display_LogStatus();
    }

    /* Log CAN status every 5 s */
    static uint32_t last_can_stat_tick = 0;
    if (now - last_can_stat_tick >= 5000)
    {
        CAN_Display_LogStatus();
        last_can_stat_tick = now;
    }

    /* Log SPWM state every 2 s so you can verify modulation index is applied */
    static uint32_t last_spwm_log_tick = 0;
    if (now - last_spwm_log_tick >= 2000)
    {
        PWM_PrintSPWMState();
        last_spwm_log_tick = now;
    }

    /* ---------- Read encoder ADCs (raw + capped dynamic cal + degrees) ---------- */
    /* Hard limits — measured bounds that outliers cannot expand past */
#define ENC_SIN_MIN_CAP  427U
#define ENC_SIN_MAX_CAP  65388U
#define ENC_COS_MIN_CAP  608U
#define ENC_COS_MAX_CAP  64743U

    static uint16_t sin_min = ENC_SIN_MIN_CAP, sin_max = ENC_SIN_MAX_CAP;
    static uint16_t cos_min = ENC_COS_MIN_CAP, cos_max = ENC_COS_MAX_CAP;

    uint16_t raw_sin = ADC2_ReadChannel(ADC_CHANNEL_10);
    uint16_t raw_cos = ADC2_ReadChannel(ADC_CHANNEL_11);

    /* Clamp to measured bounds to reject outliers */
    uint16_t csin = raw_sin;
    uint16_t ccos = raw_cos;
    if (csin < ENC_SIN_MIN_CAP) csin = ENC_SIN_MIN_CAP;
    if (csin > ENC_SIN_MAX_CAP) csin = ENC_SIN_MAX_CAP;
    if (ccos < ENC_COS_MIN_CAP) ccos = ENC_COS_MIN_CAP;
    if (ccos > ENC_COS_MAX_CAP) ccos = ENC_COS_MAX_CAP;

    /* Dynamic bounds can only tighten inside the hard limits */
    if (csin < sin_min) sin_min = csin;
    if (csin > sin_max) sin_max = csin;
    if (ccos < cos_min) cos_min = ccos;
    if (ccos > cos_max) cos_max = ccos;

    /* Calculate degrees using calibrated min/max */
    float angle_deg = 0.0f;
    if ((sin_max > sin_min) && (cos_max > cos_min))
    {
        float sin_norm = ((float)(csin - sin_min) / (float)(sin_max - sin_min)) * 2.0f - 1.0f;
        float cos_norm = ((float)(ccos - cos_min) / (float)(cos_max - cos_min)) * 2.0f - 1.0f;
        angle_deg = atan2f(sin_norm, cos_norm) * (180.0f / (float)M_PI);
        if (angle_deg < 0.0f) angle_deg += 360.0f;
    }

    int32_t deg_i = (int32_t)angle_deg;
    uint32_t deg_f = (uint32_t)((angle_deg - (float)deg_i) * 100.0f + 0.5f);
    if (deg_f >= 100) { deg_f = 0; deg_i++; }
    MCP2221A_Printf("[ENC] SIN=%5u COS=%5u | DEG=%3ld.%02lu | BOUNDS S[%5u-%5u] C[%5u-%5u]\r\n",
                     (unsigned)raw_sin, (unsigned)raw_cos,
                     (long)deg_i, (unsigned long)deg_f,
                     (unsigned)sin_min, (unsigned)sin_max,
                     (unsigned)cos_min, (unsigned)cos_max);

    char ontime_str[64];
    OnTime_Format(ontime_str, sizeof(ontime_str));
    MCP2221A_Printf("[SEXUPDATE V4] Total on-time: %s | Boot #%lu | Session tick=%lu ms\r\n",
                     ontime_str,
                     (unsigned long)OnTime_GetBootCount(),
                     (unsigned long)HAL_GetTick());

    /* ---------- LA37S600S05KM Phase-U current sensor test ---------- */
    /* Commented out while running single-phase resistive load PWM test */
//    int64_t sum_diff = 0;
//    for (int i = 0; i < ADC_BURST_COUNT; i++)
//    {
//        HAL_ADC_Start(&hadc1);
//        HAL_ADC_PollForConversion(&hadc1, 10);
//        int32_t raw_sense = (int32_t)HAL_ADC_GetValue(&hadc1);
//        int32_t raw_ref   = (int32_t)HAL_ADC_GetValue(&hadc2);
//        sum_diff += (raw_ref - raw_sense); /* flipped sign for correct polarity */
//    }
//    int32_t avg_sum  = (int32_t)(sum_diff / ADC_BURST_COUNT);
//    int32_t avg_diff = (avg_sum - adc_zero_offset_counts) / ADC_OVERSAMPLE_RATIO;
//
//    int64_t v_diff_uV  = (avg_diff * (int64_t)ADC_VREF_MV * 1000LL) / ADC_FULL_SCALE;
//    int32_t current_mA = (int32_t)((v_diff_uV * 1000LL) / SENSOR_SENSITIVITY_UV_A);
//
//    MCP2221A_Printf("[PH_U] avg_diff=%d | Vdiff=%ld uV | I=%d mA\r\n",
//                     (int)avg_diff, (long)v_diff_uV, (int)current_mA);

    /* SPWM runs continuously in the TIM1 update ISR; no main-loop duty update needed. */

    /* Simple gate-driver fault monitor */
    if (GateDriver_IsFault())
    {
        MCP2221A_PrintLn("[GATE_DRV] FAULT asserted – PWM disabled by hardware BKIN");
    }
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
