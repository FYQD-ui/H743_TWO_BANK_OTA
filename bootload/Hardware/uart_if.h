#ifndef UART_IF_H
#define UART_IF_H

#include "main.h"

/** 发送指定长度的数据 */
HAL_StatusTypeDef UartIf_SendBuffer(const uint8_t *buffer, uint16_t length);

/** 发送字符串日志 */
HAL_StatusTypeDef UartIf_SendString(const char *text);

/** 接收指定长度的数据 */
HAL_StatusTypeDef UartIf_ReceiveBuffer(uint8_t *buffer, uint16_t length, uint32_t timeout);

#endif
