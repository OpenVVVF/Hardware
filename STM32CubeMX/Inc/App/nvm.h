/**
  ******************************************************************************
  * @file    nvm.h
  * @brief   Non-volatile memory manager backed by CY15B102Q F-RAM.
  *          Owns the FRAM memory map, boot counter, on-time counter,
  *          and motor-parameter persistence.
  ******************************************************************************
  */

#pragma once

#include "cy15b102q_driver.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FRAM memory map */
#define NVM_ADDR_SYSTEM_HEADER 0x00000U
#define NVM_ADDR_MOTOR_PARAMS  0x00020U
#define NVM_ADDR_FAULT_LOG     0x00040U
#define NVM_ADDR_USER_SETTINGS 0x00100U

#define NVM_SYSTEM_MAGIC       0x4F4E5449U   /* "ONTI" */
#define NVM_SYSTEM_HEADER_SIZE 16U
#define NVM_OLD_HEADER_SIZE    12U

typedef struct {
    uint32_t magic;
    uint32_t total_ms;
    uint32_t boot_count;
    uint16_t crc;
    uint8_t  reserved[2];
} NVM_SystemHeader_t;

typedef struct {
    float    encoder_offset_rad;
    float    pole_pairs;
    float    ld_henry;
    float    lq_henry;
    float    flux_linkage_wb;
    uint16_t crc;
    uint8_t  reserved[2];
} NVM_MotorParams_t;

/**
 * @brief  Initialise the NVM subsystem and increment the boot counter.
 * @param  fram  F-RAM driver handle.
 * @retval true  if a valid record was loaded.
 * @retval false if a fresh record was created.
 */
bool NVM_Init(CY15B102Q_HandleTypeDef *fram);

/**
 * @brief  Add elapsed milliseconds to the session on-time and persist
 *         periodically (once per second or on large jumps).
 * @param  fram       F-RAM driver handle.
 * @param  elapsed_ms Milliseconds since the last update.
 */
void NVM_Update(CY15B102Q_HandleTypeDef *fram, uint32_t elapsed_ms);

uint32_t NVM_GetTotalMs(void);
uint32_t NVM_GetBootCount(void);

/**
 * @brief  Format total on-time as a human-readable string.
 */
void NVM_Format(char *buf, size_t len);

bool NVM_ReadMotorParams(CY15B102Q_HandleTypeDef *fram, NVM_MotorParams_t *params);
bool NVM_WriteMotorParams(CY15B102Q_HandleTypeDef *fram, const NVM_MotorParams_t *params);

#ifdef __cplusplus
}
#endif
