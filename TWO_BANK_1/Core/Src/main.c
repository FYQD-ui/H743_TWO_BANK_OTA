/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, and is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/** OTA元数据结构体 */
typedef struct
{
  uint32_t magic;
  uint32_t struct_version;
  uint32_t sequence;
  uint32_t active_slot;
  uint32_t switch_slot;
  uint32_t upgrade_flag;
  uint32_t last_error;
  uint32_t app1_size;
  uint32_t app1_crc32;
  uint32_t app1_version;
  uint32_t app1_valid;
  uint32_t app2_size;
  uint32_t app2_crc32;
  uint32_t app2_version;
  uint32_t app2_valid;
  uint32_t reserved[8];
  uint32_t record_crc32;
} App_OtaMetadataTypeDef;

/** 应用控制协议帧头结构体 */
typedef struct
{
  uint32_t magic;
  uint16_t command;
  uint16_t length;
  uint32_t sequence;
  uint32_t payload_crc32;
} App_ControlFrameHeaderTypeDef;

/** 应用控制协议响应载荷结构体 */
typedef struct
{
  uint32_t status;
  uint32_t value0;
  uint32_t value1;
  uint32_t value2;
} App_ControlResponsePayloadTypeDef;

/** 进入OTA模式命令载荷结构体 */
typedef struct
{
  uint32_t target_slot;
  uint32_t image_size;
  uint32_t image_crc32;
  uint32_t image_version;
} App_ControlEnterOtaPayloadTypeDef;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/** APP1镜像起始地址 */
#define APP_FLASH_BASE                       0x08020000U
/** APP1镜像大小 */
#define APP_FLASH_SIZE                       0x000E0000U
/** APP2镜像起始地址 */
#define APP_PEER_FLASH_BASE                  0x08100000U
/** APP2镜像大小 */
#define APP_PEER_FLASH_SIZE                  0x000E0000U
/** OTA元数据区起始地址 */
#define APP_METADATA_ADDRESS                 0x081E0000U
/** OTA元数据魔术字 */
#define APP_METADATA_MAGIC                   0x4F54414DU
/** OTA元数据版本号 */
#define APP_METADATA_VERSION                 0x00010000U
/** APP1槽位编号 */
#define APP_SLOT_THIS                        1U
/** APP2槽位编号 */
#define APP_SLOT_PEER                        2U
/** 空槽位编号 */
#define APP_SLOT_NONE                        0U
/** 升级标志空闲值 */
#define APP_UPGRADE_FLAG_IDLE                0U
/** 升级标志请求值 */
#define APP_UPGRADE_FLAG_REQUEST             1U
/** 元数据所在Bank */
#define APP_METADATA_BANK                    FLASH_BANK_2
/** 元数据所在Sector */
#define APP_METADATA_SECTOR                  FLASH_SECTOR_7
/** Flash写入最小单位 */
#define APP_FLASH_PROGRAM_UNIT               32U
/** 串口文本命令缓冲长度 */
#define APP_CMD_BUFFER_LENGTH                64U
/** 应用控制协议魔术字 */
#define APP_PROTOCOL_MAGIC                   0x3141544FU
/** 应用控制协议魔术字字节串 */
#define APP_PROTOCOL_MAGIC_BYTES             "OTA1"
/** 应用控制协议头长度 */
#define APP_PROTOCOL_HEADER_SIZE             16U
/** 应用控制协议最大载荷长度 */
#define APP_PROTOCOL_MAX_PAYLOAD             32U
/** 应用控制协议命令：HELLO */
#define APP_CTRL_CMD_HELLO                   0x0101U
/** 应用控制协议命令：QUERY */
#define APP_CTRL_CMD_QUERY                   0x0102U
/** 应用控制协议命令：ENTER_OTA */
#define APP_CTRL_CMD_ENTER_OTA               0x0103U
/** 应用控制协议命令：REBOOT */
#define APP_CTRL_CMD_REBOOT                  0x0104U
/** 应用控制协议响应掩码 */
#define APP_CTRL_RESPONSE_MASK               0x8000U
/** 应用控制协议状态：成功 */
#define APP_CTRL_STATUS_OK                   0x00000000U
/** 应用控制协议状态：CRC错误 */
#define APP_CTRL_STATUS_CRC_ERROR            0x00000003U
/** 应用控制协议状态：状态错误 */
#define APP_CTRL_STATUS_STATE_ERROR          0x00000004U
/** 应用控制协议状态：槽位错误 */
#define APP_CTRL_STATUS_SLOT_ERROR           0x00000005U
/** 应用控制协议状态：Flash错误 */
#define APP_CTRL_STATUS_FLASH_ERROR          0x00000007U
/** 应用控制协议状态：参数错误 */
#define APP_CTRL_STATUS_ARGUMENT_ERROR       0x00000009U
/** 设备模式编码：APP1 */
#define APP_DEVICE_MODE_THIS                 2U
/** 应用LED翻转周期 */
#define APP_LED_TOGGLE_PERIOD_MS             500U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/** 文本命令接收缓冲区 */
static char g_app_command_buffer[APP_CMD_BUFFER_LENGTH];
/** 文本命令当前长度 */
static uint32_t g_app_command_length = 0U;
/** LED上一次翻转时刻 */
static uint32_t g_app_led_tick = 0U;
/** 协议帧接收缓冲区 */
static uint8_t g_app_protocol_buffer[APP_PROTOCOL_HEADER_SIZE + APP_PROTOCOL_MAX_PAYLOAD];
/** 协议帧当前已接收长度 */
static uint32_t g_app_protocol_length = 0U;
/** 协议帧期望总长度 */
static uint32_t g_app_protocol_expected_length = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
/** 输出一行应用日志 */
static void App_Log(const char *text);
/** 输出十六进制数值日志 */
static void App_LogHexValue(const char *label, uint32_t value);
/** 计算CRC32数值 */
static uint32_t App_CalculateCrc32(const uint8_t *buffer, uint32_t length);
/** 检查栈顶地址是否合法 */
static uint32_t App_IsValidStackPointer(uint32_t stack_pointer);
/** 检查镜像向量表是否合法 */
static uint32_t App_IsApplicationValid(uint32_t app_address, uint32_t app_size);
/** 从Flash读取元数据 */
static uint32_t App_LoadMetadata(App_OtaMetadataTypeDef *metadata);
/** 生成应用侧默认元数据视图 */
static void App_CreateDefaultMetadata(App_OtaMetadataTypeDef *metadata);
/** 擦除元数据扇区 */
static HAL_StatusTypeDef App_EraseMetadataSector(void);
/** 向Flash写入任意长度数据 */
static HAL_StatusTypeDef App_WriteFlashBuffer(uint32_t address, const uint8_t *buffer, uint32_t length);
/** 将元数据保存到Flash */
static HAL_StatusTypeDef App_SaveMetadata(App_OtaMetadataTypeDef *metadata);
/** 获取当前设备模式编码 */
static uint32_t App_GetDeviceMode(void);
/** 打包当前设备状态 */
static void App_PackDeviceState(uint32_t *value0, uint32_t *value1, uint32_t *value2);
/** 发送协议响应帧 */
static void App_SendControlResponse(uint16_t command, uint32_t status, uint32_t value0, uint32_t value1, uint32_t value2);
/** 重置协议接收状态机 */
static void App_ResetProtocolParser(void);
/** 处理协议帧 */
static void App_HandleProtocolFrame(const uint8_t *frame, uint32_t frame_length);
/** 按字节处理文本命令 */
static void App_ProcessTextByte(uint8_t rx_byte);
/** 按字节处理串口输入 */
static void App_ProcessRxByte(uint8_t rx_byte);
/** 轮询串口输入数据 */
static void App_PollSerialInput(void);
/** 处理文本命令 */
static void App_ProcessSerialCommand(const char *command);
/** 写入升级请求标志 */
static HAL_StatusTypeDef App_RequestOtaFlag(uint32_t target_slot);
/** 打印当前应用信息 */
static void App_PrintInfo(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/** 输出一行应用日志 */
static void App_Log(const char *text)
{
  size_t length = strlen(text);

  (void)HAL_UART_Transmit(&huart3, (uint8_t *)text, (uint16_t)length, HAL_MAX_DELAY);
}

/** 输出十六进制数值日志 */
static void App_LogHexValue(const char *label, uint32_t value)
{
  char text[96];

  (void)snprintf(text, sizeof(text), "%s0x%08lX\r\n", label, (unsigned long)value);
  App_Log(text);
}

/** 计算CRC32数值 */
static uint32_t App_CalculateCrc32(const uint8_t *buffer, uint32_t length)
{
  uint32_t crc = 0xFFFFFFFFU;
  uint32_t index;
  uint32_t bit_index;

  if (buffer == NULL)
  {
    return 0U;
  }

  for (index = 0U; index < length; index++)
  {
    crc ^= buffer[index];
    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (crc >> 1U) ^ 0xEDB88320U;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }

  return ~crc;
}

/** 检查栈顶地址是否合法 */
static uint32_t App_IsValidStackPointer(uint32_t stack_pointer)
{
  if ((stack_pointer >= 0x20000000U) && (stack_pointer < 0x20020000U))
  {
    return 1U;
  }

  if ((stack_pointer >= 0x24000000U) && (stack_pointer < 0x24080000U))
  {
    return 1U;
  }

  if ((stack_pointer >= 0x30000000U) && (stack_pointer < 0x30048000U))
  {
    return 1U;
  }

  if ((stack_pointer >= 0x38000000U) && (stack_pointer < 0x38010000U))
  {
    return 1U;
  }

  return 0U;
}

/** 检查镜像向量表是否合法 */
static uint32_t App_IsApplicationValid(uint32_t app_address, uint32_t app_size)
{
  uint32_t stack_pointer = *((volatile uint32_t *)app_address);
  uint32_t reset_handler = *((volatile uint32_t *)(app_address + 4U));
  uint32_t reset_handler_address = reset_handler & ~1U;

  if (App_IsValidStackPointer(stack_pointer) == 0U)
  {
    return 0U;
  }

  if ((reset_handler & 1U) == 0U)
  {
    return 0U;
  }

  if ((reset_handler_address < app_address) || (reset_handler_address >= (app_address + app_size)))
  {
    return 0U;
  }

  return 1U;
}

/** 从Flash读取元数据 */
static uint32_t App_LoadMetadata(App_OtaMetadataTypeDef *metadata)
{
  const App_OtaMetadataTypeDef *flash_metadata = (const App_OtaMetadataTypeDef *)APP_METADATA_ADDRESS;
  uint32_t crc32;

  if (metadata == NULL)
  {
    return 0U;
  }

  memcpy(metadata, flash_metadata, sizeof(App_OtaMetadataTypeDef));
  if (metadata->magic != APP_METADATA_MAGIC)
  {
    return 0U;
  }

  if (metadata->struct_version != APP_METADATA_VERSION)
  {
    return 0U;
  }

  crc32 = App_CalculateCrc32((const uint8_t *)metadata, offsetof(App_OtaMetadataTypeDef, record_crc32));
  if (metadata->record_crc32 != crc32)
  {
    return 0U;
  }

  return 1U;
}

/** 生成应用侧默认元数据视图 */
static void App_CreateDefaultMetadata(App_OtaMetadataTypeDef *metadata)
{
  if (metadata == NULL)
  {
    return;
  }

  memset(metadata, 0, sizeof(App_OtaMetadataTypeDef));
  metadata->magic = APP_METADATA_MAGIC;
  metadata->struct_version = APP_METADATA_VERSION;
  metadata->sequence = 1U;
  metadata->active_slot = APP_SLOT_THIS;
  metadata->switch_slot = APP_SLOT_NONE;
  metadata->upgrade_flag = APP_UPGRADE_FLAG_IDLE;
  metadata->last_error = 0U;
  metadata->app1_valid = App_IsApplicationValid(APP_FLASH_BASE, APP_FLASH_SIZE);
  metadata->app2_valid = App_IsApplicationValid(APP_PEER_FLASH_BASE, APP_PEER_FLASH_SIZE);
  metadata->record_crc32 = App_CalculateCrc32((const uint8_t *)metadata, offsetof(App_OtaMetadataTypeDef, record_crc32));
}

/** 擦除元数据扇区 */
static HAL_StatusTypeDef App_EraseMetadataSector(void)
{
  FLASH_EraseInitTypeDef erase_init = {0};
  uint32_t sector_error = 0U;
  HAL_StatusTypeDef status;

  erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase_init.Banks = APP_METADATA_BANK;
  erase_init.Sector = APP_METADATA_SECTOR;
  erase_init.NbSectors = 1U;
  erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);
  status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
  HAL_FLASH_Lock();

  App_LogHexValue("[APP1] erase metadata status=", (uint32_t)status);
  App_LogHexValue("[APP1] erase metadata sector_error=", sector_error);
  return status;
}

/** 向Flash写入任意长度数据 */
static HAL_StatusTypeDef App_WriteFlashBuffer(uint32_t address, const uint8_t *buffer, uint32_t length)
{
  HAL_StatusTypeDef status = HAL_OK;
  uint32_t current_address = address;
  uint32_t remain_length = length;
  uint32_t copy_length;
  __ALIGNED(32) static uint8_t flash_word[APP_FLASH_PROGRAM_UNIT];

  if ((buffer == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);

  while (remain_length > 0U)
  {
    memset(flash_word, 0xFF, sizeof(flash_word));
    copy_length = (remain_length > APP_FLASH_PROGRAM_UNIT) ? APP_FLASH_PROGRAM_UNIT : remain_length;
    memcpy(flash_word, buffer, copy_length);
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, current_address, (uint32_t)flash_word);
    if (status != HAL_OK)
    {
      break;
    }

    current_address += APP_FLASH_PROGRAM_UNIT;
    buffer += copy_length;
    remain_length -= copy_length;
  }

  HAL_FLASH_Lock();
  App_LogHexValue("[APP1] write metadata status=", (uint32_t)status);
  return status;
}

/** 将元数据保存到Flash */
static HAL_StatusTypeDef App_SaveMetadata(App_OtaMetadataTypeDef *metadata)
{
  HAL_StatusTypeDef status;

  if (metadata == NULL)
  {
    return HAL_ERROR;
  }

  metadata->magic = APP_METADATA_MAGIC;
  metadata->struct_version = APP_METADATA_VERSION;
  metadata->sequence += 1U;
  metadata->record_crc32 = App_CalculateCrc32((const uint8_t *)metadata, offsetof(App_OtaMetadataTypeDef, record_crc32));

  status = App_EraseMetadataSector();
  if (status != HAL_OK)
  {
    return status;
  }

  return App_WriteFlashBuffer(APP_METADATA_ADDRESS, (const uint8_t *)metadata, sizeof(App_OtaMetadataTypeDef));
}

/** 获取当前设备模式编码 */
static uint32_t App_GetDeviceMode(void)
{
  return APP_DEVICE_MODE_THIS;
}

/** 打包当前设备状态 */
static void App_PackDeviceState(uint32_t *value0, uint32_t *value1, uint32_t *value2)
{
  App_OtaMetadataTypeDef metadata = {0};
  uint32_t state_word;
  uint32_t valid_word;

  if (App_LoadMetadata(&metadata) == 0U)
  {
    App_CreateDefaultMetadata(&metadata);
  }

  if (value0 != NULL)
  {
    *value0 = App_GetDeviceMode();
  }

  state_word = (metadata.active_slot & 0xFFU) |
               ((metadata.switch_slot & 0xFFU) << 8U) |
               ((metadata.upgrade_flag & 0xFFFFU) << 16U);
  valid_word = (metadata.app1_valid & 0xFFFFU) |
               ((metadata.app2_valid & 0xFFFFU) << 16U);

  if (value1 != NULL)
  {
    *value1 = state_word;
  }

  if (value2 != NULL)
  {
    *value2 = valid_word;
  }
}

/** 发送协议响应帧 */
static void App_SendControlResponse(uint16_t command, uint32_t status, uint32_t value0, uint32_t value1, uint32_t value2)
{
  App_ControlFrameHeaderTypeDef header = {0};
  App_ControlResponsePayloadTypeDef payload = {0};

  payload.status = status;
  payload.value0 = value0;
  payload.value1 = value1;
  payload.value2 = value2;

  header.magic = APP_PROTOCOL_MAGIC;
  header.command = (uint16_t)(command | APP_CTRL_RESPONSE_MASK);
  header.length = (uint16_t)sizeof(payload);
  header.sequence = 0U;
  header.payload_crc32 = App_CalculateCrc32((const uint8_t *)&payload, sizeof(payload));

  (void)HAL_UART_Transmit(&huart3, (uint8_t *)&header, (uint16_t)sizeof(header), HAL_MAX_DELAY);
  (void)HAL_UART_Transmit(&huart3, (uint8_t *)&payload, (uint16_t)sizeof(payload), HAL_MAX_DELAY);
}

/** 重置协议接收状态机 */
static void App_ResetProtocolParser(void)
{
  g_app_protocol_length = 0U;
  g_app_protocol_expected_length = 0U;
  memset(g_app_protocol_buffer, 0, sizeof(g_app_protocol_buffer));
}

/** 写入升级请求标志 */
static HAL_StatusTypeDef App_RequestOtaFlag(uint32_t target_slot)
{
  App_OtaMetadataTypeDef metadata = {0};

  if ((target_slot != APP_SLOT_THIS) && (target_slot != APP_SLOT_PEER))
  {
    return HAL_ERROR;
  }

  if (App_LoadMetadata(&metadata) == 0U)
  {
    App_CreateDefaultMetadata(&metadata);
  }

  metadata.active_slot = APP_SLOT_THIS;
  metadata.switch_slot = APP_SLOT_NONE;
  metadata.upgrade_flag = APP_UPGRADE_FLAG_REQUEST;
  metadata.app1_valid = App_IsApplicationValid(APP_FLASH_BASE, APP_FLASH_SIZE);
  metadata.app2_valid = App_IsApplicationValid(APP_PEER_FLASH_BASE, APP_PEER_FLASH_SIZE);

  App_Log("[APP1] ota request saved\r\n");
  return App_SaveMetadata(&metadata);
}

/** 打印当前应用信息 */
static void App_PrintInfo(void)
{
  uint32_t value0 = 0U;
  uint32_t value1 = 0U;
  uint32_t value2 = 0U;

  App_PackDeviceState(&value0, &value1, &value2);
  App_Log("[APP1] app1 running\r\n");
  App_LogHexValue("[APP1] base=", APP_FLASH_BASE);
  App_LogHexValue("[APP1] metadata=", APP_METADATA_ADDRESS);
  App_LogHexValue("[APP1] mode=", value0);
  App_LogHexValue("[APP1] state=", value1);
  App_LogHexValue("[APP1] valid=", value2);
}

/** 处理协议帧 */
static void App_HandleProtocolFrame(const uint8_t *frame, uint32_t frame_length)
{
  App_ControlFrameHeaderTypeDef header = {0};
  uint32_t value0 = 0U;
  uint32_t value1 = 0U;
  uint32_t value2 = 0U;

  if ((frame == NULL) || (frame_length < APP_PROTOCOL_HEADER_SIZE))
  {
    return;
  }

  memcpy(&header, frame, sizeof(header));
  if (header.magic != APP_PROTOCOL_MAGIC)
  {
    return;
  }

  if (frame_length != (APP_PROTOCOL_HEADER_SIZE + header.length))
  {
    App_SendControlResponse(header.command, APP_CTRL_STATUS_ARGUMENT_ERROR, 0U, 0U, 0U);
    return;
  }

  if ((header.length > 0U) &&
      (header.payload_crc32 != App_CalculateCrc32(frame + APP_PROTOCOL_HEADER_SIZE, header.length)))
  {
    App_SendControlResponse(header.command, APP_CTRL_STATUS_CRC_ERROR, 0U, 0U, 0U);
    return;
  }

  switch (header.command)
  {
    case APP_CTRL_CMD_HELLO:
    case APP_CTRL_CMD_QUERY:
      if (header.length != 0U)
      {
        App_SendControlResponse(header.command, APP_CTRL_STATUS_ARGUMENT_ERROR, 0U, 0U, 0U);
        return;
      }

      App_PackDeviceState(&value0, &value1, &value2);
      App_SendControlResponse(header.command, APP_CTRL_STATUS_OK, value0, value1, value2);
      return;

    case APP_CTRL_CMD_ENTER_OTA:
    {
      App_ControlEnterOtaPayloadTypeDef enter_ota = {0};

      if (header.length != sizeof(App_ControlEnterOtaPayloadTypeDef))
      {
        App_SendControlResponse(header.command, APP_CTRL_STATUS_ARGUMENT_ERROR, 0U, 0U, 0U);
        return;
      }

      memcpy(&enter_ota, frame + APP_PROTOCOL_HEADER_SIZE, sizeof(enter_ota));
      if ((enter_ota.target_slot != APP_SLOT_THIS) && (enter_ota.target_slot != APP_SLOT_PEER))
      {
        App_SendControlResponse(header.command, APP_CTRL_STATUS_SLOT_ERROR, enter_ota.target_slot, 0U, 0U);
        return;
      }

      if (enter_ota.target_slot == APP_SLOT_THIS)
      {
        App_SendControlResponse(header.command, APP_CTRL_STATUS_SLOT_ERROR, enter_ota.target_slot, APP_SLOT_THIS, 0U);
        return;
      }

      if (App_RequestOtaFlag(enter_ota.target_slot) != HAL_OK)
      {
        App_SendControlResponse(header.command, APP_CTRL_STATUS_FLASH_ERROR, enter_ota.target_slot, 0U, 0U);
        return;
      }

      App_Log("[APP1] enter ota by script\r\n");
      App_SendControlResponse(header.command, APP_CTRL_STATUS_OK, enter_ota.target_slot, APP_SLOT_THIS, enter_ota.image_version);
      HAL_Delay(50);
      NVIC_SystemReset();
      return;
    }

    case APP_CTRL_CMD_REBOOT:
      if (header.length != 0U)
      {
        App_SendControlResponse(header.command, APP_CTRL_STATUS_ARGUMENT_ERROR, 0U, 0U, 0U);
        return;
      }

      App_Log("[APP1] reboot by script\r\n");
      App_SendControlResponse(header.command, APP_CTRL_STATUS_OK, APP_SLOT_THIS, 0U, 0U);
      HAL_Delay(50);
      NVIC_SystemReset();
      return;

    default:
      App_SendControlResponse(header.command, APP_CTRL_STATUS_ARGUMENT_ERROR, header.command, 0U, 0U);
      return;
  }
}

/** 按字节处理文本命令 */
static void App_ProcessTextByte(uint8_t rx_byte)
{
  if ((rx_byte == '\r') || (rx_byte == '\n'))
  {
    if (g_app_command_length > 0U)
    {
      g_app_command_buffer[g_app_command_length] = '\0';
      App_ProcessSerialCommand(g_app_command_buffer);
      g_app_command_length = 0U;
    }
    return;
  }

  if (g_app_command_length < (APP_CMD_BUFFER_LENGTH - 1U))
  {
    g_app_command_buffer[g_app_command_length] = (char)rx_byte;
    g_app_command_length++;
  }
  else
  {
    g_app_command_length = 0U;
    App_Log("[APP1] command too long\r\n");
  }
}

/** 按字节处理串口输入 */
static void App_ProcessRxByte(uint8_t rx_byte)
{
  static const uint8_t magic_bytes[4] = {'O', 'T', 'A', '1'};
  uint32_t index;
  App_ControlFrameHeaderTypeDef header = {0};

  if (g_app_protocol_length == 0U)
  {
    if (rx_byte == magic_bytes[0])
    {
      g_app_protocol_buffer[0] = rx_byte;
      g_app_protocol_length = 1U;
      g_app_protocol_expected_length = APP_PROTOCOL_HEADER_SIZE;
      return;
    }

    App_ProcessTextByte(rx_byte);
    return;
  }

  if (g_app_protocol_length < sizeof(g_app_protocol_buffer))
  {
    g_app_protocol_buffer[g_app_protocol_length] = rx_byte;
    g_app_protocol_length++;
  }
  else
  {
    App_ResetProtocolParser();
    App_Log("[APP1] protocol buffer overflow\r\n");
    return;
  }

  if (g_app_protocol_length <= 4U)
  {
    if (g_app_protocol_buffer[g_app_protocol_length - 1U] != magic_bytes[g_app_protocol_length - 1U])
    {
      for (index = 0U; index < g_app_protocol_length; index++)
      {
        App_ProcessTextByte(g_app_protocol_buffer[index]);
      }
      App_ResetProtocolParser();
    }
    return;
  }

  if (g_app_protocol_length == APP_PROTOCOL_HEADER_SIZE)
  {
    memcpy(&header, g_app_protocol_buffer, sizeof(header));
    if ((header.magic != APP_PROTOCOL_MAGIC) || (header.length > APP_PROTOCOL_MAX_PAYLOAD))
    {
      App_ResetProtocolParser();
      App_Log("[APP1] invalid protocol header\r\n");
      return;
    }

    g_app_protocol_expected_length = APP_PROTOCOL_HEADER_SIZE + header.length;
  }

  if ((g_app_protocol_expected_length != 0U) && (g_app_protocol_length >= g_app_protocol_expected_length))
  {
    App_HandleProtocolFrame(g_app_protocol_buffer, g_app_protocol_expected_length);
    App_ResetProtocolParser();
  }
}

/** 轮询串口输入数据 */
static void App_PollSerialInput(void)
{
  uint8_t rx_byte;

  while (HAL_UART_Receive(&huart3, &rx_byte, 1U, 0U) == HAL_OK)
  {
    App_ProcessRxByte(rx_byte);
  }
}

/** 处理文本命令 */
static void App_ProcessSerialCommand(const char *command)
{
  if (command == NULL)
  {
    return;
  }

  if ((strcmp(command, "ping") == 0) || (strcmp(command, "hello") == 0))
  {
    App_Log("[APP1] pong\r\n");
    return;
  }

  if ((strcmp(command, "info") == 0) || (strcmp(command, "query") == 0))
  {
    App_PrintInfo();
    return;
  }

  if ((strcmp(command, "ota") == 0) || (strcmp(command, "upgrade") == 0))
  {
    if (App_RequestOtaFlag(APP_SLOT_PEER) != HAL_OK)
    {
      App_Log("[APP1] ota request save failed\r\n");
      return;
    }

    App_Log("[APP1] ota request accepted, reset now\r\n");
    HAL_Delay(50);
    NVIC_SystemReset();
    return;
  }

  if (strcmp(command, "reboot") == 0)
  {
    App_Log("[APP1] reboot now\r\n");
    HAL_Delay(50);
    NVIC_SystemReset();
    return;
  }

  App_Log("[APP1] unknown command\r\n");
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  SCB->VTOR = APP_FLASH_BASE;
  __DSB();
  __ISB();

  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_RESET);
  g_app_led_tick = HAL_GetTick();
  App_ResetProtocolParser();
  App_Log("\r\n[APP1] started from 0x08020000\r\n");
  App_Log("[APP1] control protocol ready\r\n");
  App_Log("[APP1] text command: ping | info | ota | reboot\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    App_PollSerialInput();
    if ((HAL_GetTick() - g_app_led_tick) >= APP_LED_TOGGLE_PERIOD_MS)
    {
      g_app_led_tick = HAL_GetTick();
      HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_2);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

 /* MPU Configuration */
static void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x08000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_2MB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress = 0x30040000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
