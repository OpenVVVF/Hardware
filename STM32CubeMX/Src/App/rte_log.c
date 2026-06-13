/**
  ******************************************************************************
  * @file    rte_log.c
  * @brief   Run-time event logger implementation.
  ******************************************************************************
  */

#include "rte_log.h"
#include "mcp2221a_driver.h"
#include "main.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    uint32_t timestamp_ms;
    RteLogLevel_t level;
    char tag[RTE_LOG_MAX_TAG_LEN + 1U];
    char message[RTE_LOG_BUF_SIZE];
} RteLogEntry_t;

static struct {
    RteLogEntry_t entries[RTE_LOG_HISTORY_LEN];
    uint16_t head;
    uint16_t count;
    uint32_t message_count;
} s_log;

void RTE_LogInit(void)
{
    memset(&s_log, 0, sizeof(s_log));
}

const char *RTE_LogLevelName(RteLogLevel_t level)
{
    switch (level) {
        case RTE_LOG_LEVEL_DEBUG: return "DEBUG";
        case RTE_LOG_LEVEL_INFO:  return "INFO ";
        case RTE_LOG_LEVEL_WARN:  return "WARN ";
        case RTE_LOG_LEVEL_ERROR: return "ERROR";
        case RTE_LOG_LEVEL_FATAL: return "FATAL";
        default:                  return "?????";
    }
}

void RTE_Log(RteLogLevel_t level, const char *tag, const char *fmt, ...)
{
    if (level < RTE_LOG_LEVEL_DEBUG || level > RTE_LOG_LEVEL_FATAL) {
        level = RTE_LOG_LEVEL_DEBUG;
    }

    const char *level_name = RTE_LogLevelName(level);
    const uint32_t now_ms = HAL_GetTick();

    va_list args;
    va_start(args, fmt);

    char message[RTE_LOG_BUF_SIZE];
    vsnprintf(message, sizeof(message), fmt, args);
    message[sizeof(message) - 1U] = '\0';

    va_end(args);

    /* Store a copy in the circular history buffer. */
    RteLogEntry_t *entry = &s_log.entries[s_log.head];
    entry->timestamp_ms = now_ms;
    entry->level = level;
    strncpy(entry->tag, tag, RTE_LOG_MAX_TAG_LEN);
    entry->tag[RTE_LOG_MAX_TAG_LEN] = '\0';
    strncpy(entry->message, message, sizeof(entry->message) - 1U);
    entry->message[sizeof(entry->message) - 1U] = '\0';

    s_log.head = (s_log.head + 1U) % RTE_LOG_HISTORY_LEN;
    if (s_log.count < RTE_LOG_HISTORY_LEN) {
        s_log.count++;
    }
    s_log.message_count++;

    /* Emit immediately over the debug UART. */
    MCP2221A_Printf("[%7lu ms][%s][%s] %s\r\n",
                    (unsigned long)now_ms,
                    level_name,
                    entry->tag,
                    message);
}
