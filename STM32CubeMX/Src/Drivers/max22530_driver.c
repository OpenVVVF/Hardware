/**
  ******************************************************************************
  * @file    max22530_driver.c
  * @brief   MAX22530 4-channel isolated 12-bit ADC driver.
  *
  * SPI mode: CPOL=0, CPHA=0, MSB first, 24-bit frame (8-bit cmd + 16-bit data).
  * Command byte: [A5:A0][R/W][Burst] where Read=0, Write=1.
  * ADC data is right-justified 12-bit in the 16-bit register value.
  ******************************************************************************
  */

#include "Drivers/max22530_driver.h"

#define MAX22530_SPI_TIMEOUT_MS  100U

/* Build a read command byte. */
static inline uint8_t build_read_cmd(uint8_t reg)
{
    return (uint8_t)((reg & 0x3FU) << 2);
}

/* Build a write command byte. */
static inline uint8_t build_write_cmd(uint8_t reg)
{
    return (uint8_t)(((reg & 0x3FU) << 2) | 0x02U);
}

static inline void cs_low(MAX22530_HandleTypeDef *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

static inline void cs_high(MAX22530_HandleTypeDef *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

/* Ensure the HAL SPI handle is configured for the MAX22530 (MSB first, Mode 0).
 * The CubeMX-generated hspi1 may be LSB first; this reconfigures safely.     */
static HAL_StatusTypeDef MAX22530_ReconfigureSpi(MAX22530_HandleTypeDef *dev)
{
    SPI_HandleTypeDef *hspi = dev->hspi;

    /* Only change what matters; leave pin mappings and clock alone. */
    hspi->Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi->Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi->Init.CLKPhase = SPI_PHASE_1EDGE;

    /* Keep 8-bit data size so we can build the 24-bit frame in software.
     * If CubeMX configured 16-bit, switch back to 8-bit.                  */
    hspi->Init.DataSize = SPI_DATASIZE_8BIT;

    /* Slow enough to be safe across board layouts (<= 10 MHz max).       */
    hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;

    return HAL_SPI_Init(hspi);
}

HAL_StatusTypeDef MAX22530_Init(MAX22530_HandleTypeDef *dev)
{
    if (dev == NULL || dev->hspi == NULL) {
        return HAL_ERROR;
    }

    cs_high(dev);

    if (MAX22530_ReconfigureSpi(dev) != HAL_OK) {
        return HAL_ERROR;
    }

    /* Small delay for power-up/line stabilization. */
    HAL_Delay(2);

    /* Verify device presence by reading product ID register. */
    uint16_t prod_id = MAX22530_ReadRegister(dev, MAX22530_REG_PROD_ID);

    /* Product ID 0x81 lives in the high byte of PROD_ID per datasheet. */
    if (((prod_id >> 8) & 0xFFU) != MAX22530_PRODUCT_ID) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

uint16_t MAX22530_ReadRegister(MAX22530_HandleTypeDef *dev, uint8_t reg)
{
    uint8_t tx[3];
    uint8_t rx[3] = {0};

    tx[0] = build_read_cmd(reg);
    tx[1] = 0x00U;
    tx[2] = 0x00U;

    cs_low(dev);
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(dev->hspi, tx, rx, 3U,
                                                       MAX22530_SPI_TIMEOUT_MS);
    cs_high(dev);

    if (status != HAL_OK) {
        return 0xFFFFU;
    }

    /* 16-bit data comes back in the last two bytes, MSB first.
     * The 12-bit ADC result is left-justified in bits [15:4]. */
    return (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 4);
}

HAL_StatusTypeDef MAX22530_WriteRegister(MAX22530_HandleTypeDef *dev,
                                         uint8_t reg, uint16_t value)
{
    uint8_t tx[3];

    tx[0] = build_write_cmd(reg);
    tx[1] = (uint8_t)((value >> 8) & 0xFFU);
    tx[2] = (uint8_t)(value & 0xFFU);

    cs_low(dev);
    HAL_StatusTypeDef status = HAL_SPI_Transmit(dev->hspi, tx, 3U,
                                                MAX22530_SPI_TIMEOUT_MS);
    cs_high(dev);

    return status;
}

uint16_t MAX22530_ReadAdcCounts(MAX22530_HandleTypeDef *dev, uint8_t channel)
{
    if (channel < 1U || channel > 4U) {
        return 0U;
    }
    return MAX22530_ReadRegister(dev, MAX22530_REG_ADC1 + (channel - 1U));
}

uint16_t MAX22530_ReadFilteredCounts(MAX22530_HandleTypeDef *dev, uint8_t channel)
{
    if (channel < 1U || channel > 4U) {
        return 0U;
    }
    return MAX22530_ReadRegister(dev, MAX22530_REG_FADC1 + (channel - 1U));
}

float MAX22530_ReadVoltage(MAX22530_HandleTypeDef *dev, uint8_t channel)
{
    uint16_t counts = MAX22530_ReadAdcCounts(dev, channel);
    return ((float)counts * MAX22530_VREF_VOLTS) /
           (float)MAX22530_COUNTS_FULL_SCALE;
}
