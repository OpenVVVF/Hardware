/**
  ******************************************************************************
  * @file    mcp2221a_driver.c
  * @brief   Microchip MCP2221A USB-to-UART bridge driver implementation.
  ******************************************************************************
  */
#include "mcp2221a_driver.h"
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Private state                                                             */
/* -------------------------------------------------------------------------- */

static UART_HandleTypeDef *mcp2221a_huart = NULL;
static char mcp2221a_tx_buf[MCP2221A_TX_BUF_SIZE];

/* -------------------------------------------------------------------------- */
/*  Internal helpers                                                          */
/* -------------------------------------------------------------------------- */

static inline void mcp2221a_send_byte(uint8_t b)
{
    if (mcp2221a_huart != NULL)
    {
        HAL_UART_Transmit(mcp2221a_huart, &b, 1U, HAL_MAX_DELAY);
    }
}

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

void MCP2221A_Init(UART_HandleTypeDef *huart)
{
    mcp2221a_huart = huart;

    /* Wait for the MCP2221A to complete USB enumeration on the host.
       Until enumeration finishes, bytes transmitted by the STM32 are
       discarded because the CDC interface is not yet active.            */
    HAL_Delay(MCP2221A_ENUM_DELAY_MS);

    MCP2221A_PrintLn("[MCP2221A] USB CDC bridge ready");
}

void MCP2221A_Transmit(const uint8_t *data, uint16_t len)
{
    if (mcp2221a_huart != NULL && data != NULL && len > 0U)
    {
        HAL_UART_Transmit(mcp2221a_huart, (uint8_t *)data, len, HAL_MAX_DELAY);
    }
}

void MCP2221A_Print(const char *str)
{
    if (str != NULL)
    {
        MCP2221A_Transmit((const uint8_t *)str, (uint16_t)strlen(str));
    }
}

void MCP2221A_PrintLn(const char *str)
{
    MCP2221A_Print(str);
    MCP2221A_Transmit((const uint8_t *)"\r\n", 2U);
}

void MCP2221A_Printf(const char *fmt, ...)
{
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(mcp2221a_tx_buf, sizeof(mcp2221a_tx_buf), fmt, args);
    va_end(args);

    if (len > 0)
    {
        if (len >= (int)sizeof(mcp2221a_tx_buf))
        {
            len = (int)sizeof(mcp2221a_tx_buf) - 1;
        }
        MCP2221A_Transmit((const uint8_t *)mcp2221a_tx_buf, (uint16_t)len);
    }
}

/* -------------------------------------------------------------------------- */
/*  Newlib/Picolibc integration                                               */
/* -------------------------------------------------------------------------- */

int __io_putchar(int ch)
{
    mcp2221a_send_byte((uint8_t)ch);
    return ch;
}
