/**
  ******************************************************************************
  * @file    ontime_logger.h
  * @brief   Persistent system on-time tracker using CY15B102Q F-RAM.
  *
  *          Stores total accumulated power-on time in milliseconds.
  *          Data survives resets and power cycles because F-RAM is
  *          non-volatile with unlimited write endurance.
  ******************************************************************************
  */
#ifndef ONTIME_LOGGER_H
#define ONTIME_LOGGER_H

#include "cy15b102q_driver.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* F-RAM storage address for the on-time record */
#define ONTIME_FRAM_ADDR      0x00000U

/* Magic number to validate record integrity ("ONTI") */
#define ONTIME_MAGIC          0x4F4E5449U

/* -------------------------------------------------------------------------- */
/*  API                                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief  Initialize the on-time logger.
 *         Reads the previous total from F-RAM (if valid) or initializes
 *         a fresh record on first boot.
 * @param  fram  Pointer to initialized CY15B102Q handle.
 * @retval true if valid previous data was found, false if fresh init.
 */
bool OnTime_Init(CY15B102Q_HandleTypeDef *fram);

/**
 * @brief  Update and persist the current on-time.
 *         Should be called periodically (e.g., once per second).
 */
void OnTime_Update(void);

/**
 * @brief  Get total accumulated on-time in milliseconds.
 */
uint32_t OnTime_GetTotalMs(void);

/**
 * @brief  Get the number of system boots recorded.
 */
uint32_t OnTime_GetBootCount(void);

/**
 * @brief  Format on-time into a human-readable string.
 * @param  buf  Destination buffer.
 * @param  len  Buffer size (recommended >= 64).
 */
void OnTime_Format(char *buf, size_t len);

#endif /* ONTIME_LOGGER_H */
