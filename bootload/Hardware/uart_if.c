#include "uart_if.h"
#include "usart.h"
#include <string.h>

/** 发送指定长度的数据 */
HAL_StatusTypeDef UartIf_SendBuffer(const uint8_t *buffer, uint16_t length)
{
  if ((buffer == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  return HAL_UART_Transmit(&huart3, (uint8_t *)buffer, length, HAL_MAX_DELAY);
}

/** 发送字符串日志 */
HAL_StatusTypeDef UartIf_SendString(const char *text)
{
  if (text == NULL)
  {
    return HAL_ERROR;
  }

  return UartIf_SendBuffer((const uint8_t *)text, (uint16_t)strlen(text));
}

/** 接收指定长度的数据 */
HAL_StatusTypeDef UartIf_ReceiveBuffer(uint8_t *buffer, uint16_t length, uint32_t timeout)
{
  if ((buffer == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  return HAL_UART_Receive(&huart3, buffer, length, timeout);
}
