/**
  ******************************************************************************
  * @file    adc_backend.c
  * @brief   STM32 ADC backend implementation.
  ******************************************************************************
  */

#include "adc_backend.h"
#include "adc.h"
#include "spi.h"
#include "Drivers/max22530_driver.h"
#include "main.h"
#include <string.h>

#define ADC_VREF_MV      3300U
#define ADC_FS_16BIT     65535U
#define ADC_FS_12BIT     4095U
#define ADC_POLL_TIMEOUT 10U

/* Isolated voltage-sense ADC on SPI1 (MAX22530). */
static MAX22530_HandleTypeDef s_vdc_adc = {
    .hspi    = &hspi1,
    .cs_port = SPI1_CS_GPIO_Port,
    .cs_pin  = SPI1_CS_Pin,
};

HAL_StatusTypeDef AdcBackend_Init(void)
{
    return MAX22530_Init(&s_vdc_adc);
}

MAX22530_HandleTypeDef *AdcBackend_GetVdcAdc(void)
{
    return &s_vdc_adc;
}

static void ADC_ConfigChannel(ADC_HandleTypeDef *hadc, uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_32CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    HAL_ADC_ConfigChannel(hadc, &sConfig);
}

static uint16_t ADC_ReadAdc1Channel(uint32_t channel)
{
    ADC_ConfigChannel(&hadc1, channel);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, ADC_POLL_TIMEOUT);
    uint16_t adc1_val = (uint16_t)HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_GetValue(&hadc2); /* discard slave result */
    return adc1_val;
}

static uint16_t ADC_ReadAdc2Channel(uint32_t channel)
{
    ADC_ConfigChannel(&hadc2, channel);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, ADC_POLL_TIMEOUT);
    (void)HAL_ADC_GetValue(&hadc1); /* discard master result */
    return (uint16_t)HAL_ADC_GetValue(&hadc2);
}

static uint16_t ADC_ReadAdc3Channel(uint32_t channel)
{
    ADC_ConfigChannel(&hadc3, channel);
    HAL_ADC_Start(&hadc3);
    HAL_ADC_PollForConversion(&hadc3, ADC_POLL_TIMEOUT);
    return (uint16_t)HAL_ADC_GetValue(&hadc3);
}

static float CountsToVolts(uint16_t counts, uint32_t full_scale)
{
    return ((float)counts * (float)ADC_VREF_MV) / ((float)full_scale * 1000.0f);
}

float AdcBackend_ReadChannelVoltage(const char *name)
{
    if (strcmp(name, "V_DC_BUS") == 0) {
        /* DC-link voltage comes from the isolated MAX22530 ADC on SPI1.
         * Hardware maps DC_LINK_VSENSE to channel 1 of the voltage-sense ADC. */
        return MAX22530_ReadVoltage(&s_vdc_adc, 1U);
    }

    if (strcmp(name, "ENCODER_SIN") == 0) {
        return CountsToVolts(ADC_ReadAdc2Channel(ADC_CHANNEL_10), ADC_FS_16BIT);
    }

    if (strcmp(name, "ENCODER_COS") == 0) {
        return CountsToVolts(ADC_ReadAdc2Channel(ADC_CHANNEL_11), ADC_FS_16BIT);
    }

    if (strcmp(name, "MOTOR_TEMP") == 0) {
        return CountsToVolts(ADC_ReadAdc3Channel(ADC_CHANNEL_9), ADC_FS_12BIT);
    }

    return 0.0f;
}
