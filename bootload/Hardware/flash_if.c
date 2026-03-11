#include "flash_if.h"
#include "uart_if.h"
#include <stdio.h>
#include <string.h>

/** Flash编程最小单位，H7为32字节 */
#define FLASH_IF_PROGRAM_UNIT         32U
/** CRC32初始值 */
#define FLASH_IF_CRC32_INIT           0xFFFFFFFFU
/** CRC32多项式 */
#define FLASH_IF_CRC32_POLY           0xEDB88320U

/** 输出Flash接口调试日志 */
static void FlashIf_Log(const char *text)
{
  (void)UartIf_SendString(text);
}

/** 输出Flash状态码和扇区错误信息 */
static void FlashIf_LogEraseResult(const char *label, HAL_StatusTypeDef status, uint32_t sector_error)
{
  char text[128];

  (void)snprintf(text,
                 sizeof(text),
                 "%sstatus=%lu sector_error=0x%08lX\r\n",
                 label,
                 (unsigned long)status,
                 (unsigned long)sector_error);
  FlashIf_Log(text);
}

/** 输出Flash写入结果 */
static void FlashIf_LogWriteResult(uint32_t address, uint32_t length, HAL_StatusTypeDef status)
{
  char text[128];

  (void)snprintf(text,
                 sizeof(text),
                 "[BOOT] flash write addr=0x%08lX len=%lu status=%lu\r\n",
                 (unsigned long)address,
                 (unsigned long)length,
                 (unsigned long)status);
  FlashIf_Log(text);
}

/** 查询槽位对应的起始地址 */
uint32_t FlashIf_GetSlotAddress(uint32_t slot)
{
  if (slot == OTA_SLOT_APP1)
  {
    return OTA_APP1_ADDRESS;
  }

  if (slot == OTA_SLOT_APP2)
  {
    return OTA_APP2_ADDRESS;
  }

  return 0U;
}

/** 查询槽位对应的可用大小 */
uint32_t FlashIf_GetSlotSize(uint32_t slot)
{
  if (slot == OTA_SLOT_APP1)
  {
    return OTA_APP1_SIZE;
  }

  if (slot == OTA_SLOT_APP2)
  {
    return OTA_APP2_SIZE;
  }

  return 0U;
}

/** 检查栈顶地址是否位于有效RAM区 */
static uint32_t FlashIf_IsValidStackPointer(uint32_t stack_pointer)
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

/** 擦除指定Bank和Sector区间 */
static HAL_StatusTypeDef FlashIf_EraseSectors(uint32_t bank, uint32_t sector, uint32_t count)
{
  FLASH_EraseInitTypeDef erase_init = {0};
  uint32_t sector_error = 0U;
  HAL_StatusTypeDef status;

  erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase_init.Banks = bank;
  erase_init.Sector = sector;
  erase_init.NbSectors = count;
  erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);
  status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
  HAL_FLASH_Lock();

  FlashIf_LogEraseResult("[BOOT] flash erase result ", status, sector_error);
  return status;
}

/** 擦除指定槽位的全部区域 */
HAL_StatusTypeDef FlashIf_EraseSlot(uint32_t slot)
{
  if (slot == OTA_SLOT_APP1)
  {
    FlashIf_Log("[BOOT] erase APP1 slot sectors\r\n");
    return FlashIf_EraseSectors(FLASH_BANK_1, FLASH_SECTOR_1, 7U);
  }

  if (slot == OTA_SLOT_APP2)
  {
    FlashIf_Log("[BOOT] erase APP2 slot sectors\r\n");
    return FlashIf_EraseSectors(FLASH_BANK_2, FLASH_SECTOR_0, 7U);
  }

  FlashIf_Log("[BOOT] erase slot failed: invalid slot\r\n");
  return HAL_ERROR;
}

/** 擦除元数据区域 */
HAL_StatusTypeDef FlashIf_EraseMetadata(void)
{
  FlashIf_Log("[BOOT] erase metadata sector start\r\n");
  return FlashIf_EraseSectors(OTA_METADATA_BANK, OTA_METADATA_SECTOR, 1U);
}

/** 向指定地址写入任意长度数据 */
HAL_StatusTypeDef FlashIf_WriteBuffer(uint32_t address, const uint8_t *buffer, uint32_t length)
{
  HAL_StatusTypeDef status = HAL_OK;
  uint32_t current_address = address;
  uint32_t remain_length = length;
  uint32_t copy_length;
  __ALIGNED(32) static uint8_t flash_word[FLASH_IF_PROGRAM_UNIT];

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
    copy_length = (remain_length > FLASH_IF_PROGRAM_UNIT) ? FLASH_IF_PROGRAM_UNIT : remain_length;
    memcpy(flash_word, buffer, copy_length);

    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, current_address, (uint32_t)flash_word);
    if (status != HAL_OK)
    {
      break;
    }

    current_address += FLASH_IF_PROGRAM_UNIT;
    buffer += copy_length;
    remain_length -= copy_length;
  }

  HAL_FLASH_Lock();
  FlashIf_LogWriteResult(address, length, status);
  return status;
}

/** 对内存缓冲区计算CRC32 */
uint32_t FlashIf_CalculateCrc32(const uint8_t *buffer, uint32_t length)
{
  uint32_t crc = FLASH_IF_CRC32_INIT;
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
        crc = (crc >> 1U) ^ FLASH_IF_CRC32_POLY;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }

  return ~crc;
}

/** 对Flash地址范围计算CRC32 */
uint32_t FlashIf_CalculateFlashCrc32(uint32_t address, uint32_t length)
{
  return FlashIf_CalculateCrc32((const uint8_t *)address, length);
}

/** 检查应用向量表是否有效 */
uint32_t FlashIf_IsApplicationValid(uint32_t app_address, uint32_t app_size)
{
  uint32_t stack_pointer = *((volatile uint32_t *)app_address);
  uint32_t reset_handler = *((volatile uint32_t *)(app_address + 4U));
  uint32_t reset_handler_address = reset_handler & ~1U;

  if (FlashIf_IsValidStackPointer(stack_pointer) == 0U)
  {
    return 0U;
  }

  if ((reset_handler & 1U) == 0U)
  {
    return 0U;
  }

  if ((reset_handler_address < app_address) ||
      (reset_handler_address >= (app_address + app_size)))
  {
    return 0U;
  }

  return 1U;
}
