/**
  ******************************************************************************
  * @file    mcp2221a_driver.h
  * @brief   Microchip MCP2221A USB-to-UART bridge driver for STM32H7
  *
  *          The MCP2221A enumerates as a USB CDC Virtual COM Port on the PC.
  *          When the PC opens the port at a specific baud rate, the MCP2221A
  *          automatically adapts its UART side to match (via SET_LINE_CODING).
  *          The STM32 side simply drives a standard UART at the same baud rate.
  *
  *          UART wiring (3.3 V logic):
  *            STM32 PB10 (USART3_TX)  ->  MCP2221A URx
  *            STM32 PB11 (USART3_RX)  <-  MCP2221A UTx
  *            GND                     ->  GND
  *
  *          The driver implements __io_putchar so printf/puts work out of the box.
  ******************************************************************************
  */
#ifndef MCP2221A_DRIVER_H
#define MCP2221A_DRIVER_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdarg.h>

/* Time to wait for the MCP2221A to finish USB enumeration after power-up.
   Typical enumeration time is 100-500 ms depending on the host OS.          */
#ifndef MCP2221A_ENUM_DELAY_MS
#define MCP2221A_ENUM_DELAY_MS  500U
#endif

/* Internal formatting buffer size for MCP2221A_Printf()                     */
#ifndef MCP2221A_TX_BUF_SIZE
#define MCP2221A_TX_BUF_SIZE    256U
#endif

/* -------------------------------------------------------------------------- */
/*  API                                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief  Bind the driver to a HAL UART handle and wait for USB enumeration.
 * @param  huart  Pointer to the initialized UART handle (e.g. &huart3).
 * @retval None
 */
void MCP2221A_Init(UART_HandleTypeDef *huart);

/**
 * @brief  Send raw bytes over the MCP2221A bridge (blocking).
 * @param  data  Pointer to data buffer.
 * @param  len   Number of bytes to send.
 * @retval None
 */
void MCP2221A_Transmit(const uint8_t *data, uint16_t len);

/**
 * @brief  Send a null-terminated string (no line ending).
 * @param  str  C-string to transmit.
 * @retval None
 */
void MCP2221A_Print(const char *str);

/**
 * @brief  Send a null-terminated string followed by \r\n.
 * @param  str  C-string to transmit.
 * @retval None
 */
void MCP2221A_PrintLn(const char *str);

/**
 * @brief  printf-style formatted output (blocking).
 * @param  fmt  printf format string.
 * @param  ...  Variadic arguments.
 * @retval None
 */
void MCP2221A_Printf(const char *fmt, ...);

/**
 * @brief  Newlib / Picolibc hook for printf/puts.
 *         Defined here so stdout is redirected to the MCP2221A VCP.
 * @param  ch  Character to transmit.
 * @retval The transmitted character.
 */
int __io_putchar(int ch);

#endif /* MCP2221A_DRIVER_H */
