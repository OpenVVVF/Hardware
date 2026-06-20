/**
  ******************************************************************************
  * @file    cy15b102q_driver.c
  * @brief   CY15B102Q 2-Mbit SPI F-RAM driver implementation.
  ******************************************************************************
  */
#include "cy15b102q_driver.h"
#include <string.h>

/* Internal timeout for HAL SPI calls (ms) */
#define CY15B102Q_SPI_TIMEOUT  100U

/* -------------------------------------------------------------------------- */
/*  Low-level pin helpers                                                     */
/* -------------------------------------------------------------------------- */

static inline void cs_low(CY15B102Q_HandleTypeDef *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

static inline void cs_high(CY15B102Q_HandleTypeDef *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

HAL_StatusTypeDef CY15B102Q_Init(CY15B102Q_HandleTypeDef *dev)
{
    /* Bring control lines to safe idle state:
       CS  = HIGH  (deselected)
       WP  = HIGH  (write protection disabled)
       HOLD = HIGH (normal operation)               */
    HAL_GPIO_WritePin(dev->cs_port,  dev->cs_pin,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->wp_port,  dev->wp_pin,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->hold_port, dev->hold_pin, GPIO_PIN_SET);

    /* Small delay for power-up / line stabilization */
    HAL_Delay(1);

    /* Verify chip presence by reading JEDEC ID */
    uint64_t id = CY15B102Q_ReadID(dev);

    /* Infineon (Cypress) manufacturer code is 0xC2 in the ID stream.
       We look for that byte somewhere in the 9-byte response.            */
    bool found = false;
    for (int i = 0; i < 8; i++) {
        if (((id >> (8 * i)) & 0xFFULL) == 0xC2U) {
            found = true;
            break;
        }
    }

    return found ? HAL_OK : HAL_ERROR;
}

uint8_t CY15B102Q_ReadStatus(CY15B102Q_HandleTypeDef *dev)
{
    uint8_t tx = CY15B102Q_RDSR;
    uint8_t rx = 0xFFU;

    cs_low(dev);
    HAL_SPI_Transmit(dev->hspi, &tx, 1U, CY15B102Q_SPI_TIMEOUT);
    HAL_SPI_Receive(dev->hspi, &rx, 1U, CY15B102Q_SPI_TIMEOUT);
    cs_high(dev);

    return rx;
}

void CY15B102Q_WriteEnable(CY15B102Q_HandleTypeDef *dev)
{
    uint8_t tx = CY15B102Q_WREN;
    cs_low(dev);
    HAL_SPI_Transmit(dev->hspi, &tx, 1U, CY15B102Q_SPI_TIMEOUT);
    cs_high(dev);
}

void CY15B102Q_WriteDisable(CY15B102Q_HandleTypeDef *dev)
{
    uint8_t tx = CY15B102Q_WRDI;
    cs_low(dev);
    HAL_SPI_Transmit(dev->hspi, &tx, 1U, CY15B102Q_SPI_TIMEOUT);
    cs_high(dev);
}

void CY15B102Q_Read(CY15B102Q_HandleTypeDef *dev, uint32_t addr,
                    uint8_t *buf, uint32_t len)
{
    if (len == 0U || buf == NULL) return;

    addr &= CY15B102Q_ADDR_MASK;

    uint8_t cmd[4];
    cmd[0] = CY15B102Q_READ;
    cmd[1] = (uint8_t)((addr >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((addr >> 8)  & 0xFFU);
    cmd[3] = (uint8_t)(addr & 0xFFU);

    cs_low(dev);
    HAL_SPI_Transmit(dev->hspi, cmd, 4U, CY15B102Q_SPI_TIMEOUT);
    HAL_SPI_Receive(dev->hspi, buf, (uint16_t)len, CY15B102Q_SPI_TIMEOUT);
    cs_high(dev);
}

void CY15B102Q_Write(CY15B102Q_HandleTypeDef *dev, uint32_t addr,
                     const uint8_t *buf, uint32_t len)
{
    if (len == 0U || buf == NULL) return;

    addr &= CY15B102Q_ADDR_MASK;

    /* F-RAM requires Write Enable before every WRITE opcode */
    CY15B102Q_WriteEnable(dev);

    uint8_t cmd[4];
    cmd[0] = CY15B102Q_WRITE;
    cmd[1] = (uint8_t)((addr >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((addr >> 8)  & 0xFFU);
    cmd[3] = (uint8_t)(addr & 0xFFU);

    cs_low(dev);
    HAL_SPI_Transmit(dev->hspi, cmd, 4U, CY15B102Q_SPI_TIMEOUT);
    HAL_SPI_Transmit(dev->hspi, (uint8_t *)buf, (uint16_t)len, CY15B102Q_SPI_TIMEOUT);
    cs_high(dev);
}

uint64_t CY15B102Q_ReadID(CY15B102Q_HandleTypeDef *dev)
{
    uint8_t tx = CY15B102Q_RDID;
    uint8_t rx[9] = {0};

    cs_low(dev);
    HAL_SPI_Transmit(dev->hspi, &tx, 1U, CY15B102Q_SPI_TIMEOUT);
    HAL_SPI_Receive(dev->hspi, rx, 9U, CY15B102Q_SPI_TIMEOUT);
    cs_high(dev);

    uint64_t id = 0;
    for (int i = 0; i < 9; i++) {
        id |= ((uint64_t)rx[i] << (8 * i));
    }
    return id;
}

void CY15B102Q_Sleep(CY15B102Q_HandleTypeDef *dev)
{
    uint8_t tx = CY15B102Q_SLEEP;
    cs_low(dev);
    HAL_SPI_Transmit(dev->hspi, &tx, 1U, CY15B102Q_SPI_TIMEOUT);
    cs_high(dev);
}

void CY15B102Q_Wake(CY15B102Q_HandleTypeDef *dev)
{
    /* Any SPI transaction with CS low wakes the device.
       We perform a harmless status register read.          */
    (void)CY15B102Q_ReadStatus(dev);
}
