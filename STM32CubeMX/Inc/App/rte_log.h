/**
  ******************************************************************************
  * @file    rte_log.h
  * @brief   Run-time event (RTE) logger.
  *          Provides timestamped, severity-graded diagnostic output over the
  *          MCP2221A debug UART.
  ******************************************************************************
  */

#pragma once

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RTE_LOG_LEVEL_DEBUG = 0,
    RTE_LOG_LEVEL_INFO,
    RTE_LOG_LEVEL_WARN,
    RTE_LOG_LEVEL_ERROR,
    RTE_LOG_LEVEL_FATAL
} RteLogLevel_t;

#define RTE_LOG_MAX_TAG_LEN  8U
#define RTE_LOG_BUF_SIZE     256U
#define RTE_LOG_HISTORY_LEN  16U

/**
 * @brief Initialise the logger. Must be called after MCP2221A_Init().
 */
void RTE_LogInit(void);

/**
 * @brief Emit a log message with the given severity and source tag.
 */
void RTE_Log(RteLogLevel_t level, const char *tag, const char *fmt, ...);

/**
 * @brief Get the string name for a severity level.
 */
const char *RTE_LogLevelName(RteLogLevel_t level);

/* Convenience macros */
#define RTE_LOGD(tag, fmt, ...) RTE_Log(RTE_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define RTE_LOGI(tag, fmt, ...) RTE_Log(RTE_LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define RTE_LOGW(tag, fmt, ...) RTE_Log(RTE_LOG_LEVEL_WARN,  tag, fmt, ##__VA_ARGS__)
#define RTE_LOGE(tag, fmt, ...) RTE_Log(RTE_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define RTE_LOGF(tag, fmt, ...) RTE_Log(RTE_LOG_LEVEL_FATAL, tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
