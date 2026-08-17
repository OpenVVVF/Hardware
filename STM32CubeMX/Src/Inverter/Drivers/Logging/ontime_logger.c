/**
  ******************************************************************************
  * @file    ontime_logger.c
  * @brief   Persistent system on-time tracker implementation.
  ******************************************************************************
  */
#include "ontime_logger.h"
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Record layout (12 bytes, fixed offsets, no struct padding issues)         */
/*  Offset 0..3 : Magic number (uint32_t)                                     */
/*  Offset 4..7 : Total on-time in ms (uint32_t)                              */
/*  Offset 8..11: Boot count (uint32_t)                                       */
/* -------------------------------------------------------------------------- */

static CY15B102Q_HandleTypeDef *g_fram = NULL;
static uint32_t g_boot_tick = 0;
static uint32_t g_stored_total_ms = 0;
static uint32_t g_boot_count = 0;

/* -------------------------------------------------------------------------- */
/*  Internal helpers                                                          */
/* -------------------------------------------------------------------------- */

static void read_record(uint32_t *magic, uint32_t *total_ms, uint32_t *boot_count)
{
    uint8_t buf[12];
    CY15B102Q_Read(g_fram, ONTIME_FRAM_ADDR, buf, 12);
    memcpy(magic,       &buf[0], 4);
    memcpy(total_ms,    &buf[4], 4);
    memcpy(boot_count,  &buf[8], 4);
}

static void write_record(void)
{
    uint8_t buf[12];
    uint32_t magic = ONTIME_MAGIC;
    memcpy(&buf[0], &magic,          4);
    memcpy(&buf[4], &g_stored_total_ms, 4);
    memcpy(&buf[8], &g_boot_count,   4);
    CY15B102Q_Write(g_fram, ONTIME_FRAM_ADDR, buf, 12);
}

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

bool OnTime_Init(CY15B102Q_HandleTypeDef *fram)
{
    g_fram = fram;
    g_boot_tick = HAL_GetTick();

    uint32_t magic = 0;
    read_record(&magic, &g_stored_total_ms, &g_boot_count);

    bool valid = (magic == ONTIME_MAGIC);
    if (!valid)
    {
        g_stored_total_ms = 0;
        g_boot_count = 0;
    }

    g_boot_count++;
    write_record();
    return valid;
}

void OnTime_Update(void)
{
    if (g_fram == NULL) return;

    uint32_t session_ms = HAL_GetTick() - g_boot_tick;
    g_stored_total_ms += session_ms;
    g_boot_tick = HAL_GetTick();   /* reset so we don't double-count */

    write_record();
}

uint32_t OnTime_GetTotalMs(void)
{
    if (g_fram == NULL) return 0;

    uint32_t session_ms = HAL_GetTick() - g_boot_tick;
    return g_stored_total_ms + session_ms;
}

uint32_t OnTime_GetBootCount(void)
{
    return g_boot_count;
}

void OnTime_Format(char *buf, size_t len)
{
    uint32_t total = OnTime_GetTotalMs();

    uint32_t ms  = total % 1000U;
    uint32_t sec = (total / 1000U) % 60U;
    uint32_t min = (total / 60000U) % 60U;
    uint32_t hr  = (total / 3600000U) % 24U;
    uint32_t day = total / 86400000U;

    snprintf(buf, len,
             "%lud %02lu:%02lu:%02lu.%03lu",
             (unsigned long)day,
             (unsigned long)hr,
             (unsigned long)min,
             (unsigned long)sec,
             (unsigned long)ms);
}
