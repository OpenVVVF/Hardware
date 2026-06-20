/**
  ******************************************************************************
  * @file    cy15b102q_driver.h
  * @brief   Infineon CY15B102Q 2-Mbit (256 K x 8) Serial SPI F-RAM driver
  *
  *          Key features:
  *          - 256 KB non-volatile memory (no erase needed, instant writes)
  *          - SPI Mode 0 (CPOL=0, CPHA=0) or Mode 3
  *          - Max 40 MHz SPI clock
  *          - No write delay (WIP bit always 0)
  *
  *          Wiring (SPI4 on STM32H723):
  *            PE2  (SPI4_SCK)   -> SCK
  *            PE5  (SPI4_MISO)  <- SO
  *            PE6  (SPI4_MOSI)  -> SI
  *            PC14 (FRAM_CS)    -> CS  (software controlled)
  *            PC13 (FRAM_WP)    -> WP  (HIGH = write enabled)
  *            PC15 (FRAM_HOLD)  -> HOLD (HIGH = normal operation)
  ******************************************************************************
  */
#ifndef CY15B102Q_DRIVER_H
#define CY15B102Q_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------- */
/*  Opcodes                                                                   */
/* -------------------------------------------------------------------------- */
#define CY15B102Q_WREN     0x06U   /* Write Enable                        */
#define CY15B102Q_WRDI     0x04U   /* Write Disable                       */
#define CY15B102Q_RDSR     0x05U   /* Read Status Register                */
#define CY15B102Q_WRSR     0x01U   /* Write Status Register               */
#define CY15B102Q_READ     0x03U   /* Read Memory Data                    */
#define CY15B102Q_WRITE    0x02U   /* Write Memory Data                   */
#define CY15B102Q_FSTRD    0x0BU   /* Fast Read (with dummy byte)         */
#define CY15B102Q_RDID     0x9FU   /* Read Device ID                      */
#define CY15B102Q_SLEEP    0xB9U   /* Enter Sleep Mode                    */

/* -------------------------------------------------------------------------- */
/*  Status register bits                                                      */
/* -------------------------------------------------------------------------- */
#define CY15B102Q_SR_WIP   0x01U   /* Write In Progress (always 0 on F-RAM) */
#define CY15B102Q_SR_WEL   0x02U   /* Write Enable Latch                  */
#define CY15B102Q_SR_BP0   0x04U   /* Block Protect bit 0                 */
#define CY15B102Q_SR_BP1   0x08U   /* Block Protect bit 1                 */
#define CY15B102Q_SR_WPEN  0x80U   /* Write Protect Enable                */

/* -------------------------------------------------------------------------- */
/*  Device geometry                                                           */
/* -------------------------------------------------------------------------- */
#define CY15B102Q_SIZE     262144UL   /* 256 KB  = 2 Mbit                  */
#define CY15B102Q_ADDR_MASK 0x3FFFFUL /* Valid address bits (A17..A0)       */

/* -------------------------------------------------------------------------- */
/*  Handle                                                                    */
/* -------------------------------------------------------------------------- */
typedef struct {
    SPI_HandleTypeDef *hspi;       /* HAL SPI handle (e.g. &hspi4)         */
    GPIO_TypeDef      *cs_port;    /* Chip Select GPIO port                */
    uint16_t           cs_pin;     /* Chip Select GPIO pin                 */
    GPIO_TypeDef      *wp_port;    /* Write Protect GPIO port              */
    uint16_t           wp_pin;     /* Write Protect GPIO pin               */
    GPIO_TypeDef      *hold_port;  /* Hold GPIO port                       */
    uint16_t           hold_pin;   /* Hold GPIO pin                        */
} CY15B102Q_HandleTypeDef;

/* -------------------------------------------------------------------------- */
/*  API                                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief  Initialize control lines (CS=HIGH, WP=HIGH, HOLD=HIGH) and
 *         optionally verify the chip by reading its ID.
 * @param  dev  Pointer to driver handle.
 * @retval HAL_OK on success, HAL_ERROR if device ID does not match.
 */
HAL_StatusTypeDef CY15B102Q_Init(CY15B102Q_HandleTypeDef *dev);

/**
 * @brief  Read the 8-bit status register.
 */
uint8_t CY15B102Q_ReadStatus(CY15B102Q_HandleTypeDef *dev);

/**
 * @brief  Set the Write Enable Latch (WEL) bit. Required before every WRITE.
 */
void CY15B102Q_WriteEnable(CY15B102Q_HandleTypeDef *dev);

/**
 * @brief  Clear the Write Enable Latch.
 */
void CY15B102Q_WriteDisable(CY15B102Q_HandleTypeDef *dev);

/**
 * @brief  Read data from F-RAM.
 * @param  addr  Start address (0 .. CY15B102Q_SIZE-1).
 * @param  buf   Destination buffer.
 * @param  len   Number of bytes to read.
 */
void CY15B102Q_Read(CY15B102Q_HandleTypeDef *dev, uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief  Write data to F-RAM (non-volatile, no erase required).
 * @param  addr  Start address (0 .. CY15B102Q_SIZE-1).
 * @param  buf   Source buffer.
 * @param  len   Number of bytes to write.
 */
void CY15B102Q_Write(CY15B102Q_HandleTypeDef *dev, uint32_t addr, const uint8_t *buf, uint32_t len);

/**
 * @brief  Read the 9-byte JEDEC device ID.
 * @retval 64-bit ID value (manufacturer + product + density).
 */
uint64_t CY15B102Q_ReadID(CY15B102Q_HandleTypeDef *dev);

/**
 * @brief  Put the device into ultra-low-power sleep mode.
 *         CS must be LOW -> send SLEEP -> CS HIGH.
 */
void CY15B102Q_Sleep(CY15B102Q_HandleTypeDef *dev);

/**
 * @brief  Wake the device from sleep (any SPI transaction wakes it).
 */
void CY15B102Q_Wake(CY15B102Q_HandleTypeDef *dev);

#ifdef __cplusplus
}
#endif

#endif /* CY15B102Q_DRIVER_H */
