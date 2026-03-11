#include "ota_metadata.h"
#include "flash_if.h"
#include "uart_if.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** 输出元数据调试日志 */
static void OtaMetadata_Log(const char *text)
{
  (void)UartIf_SendString(text);
}

/** 输出元数据保存状态 */
static void OtaMetadata_LogSaveState(const OtaMetadata_TypeDef *metadata, HAL_StatusTypeDef status, const char *stage)
{
  char text[192];

  if (metadata == NULL)
  {
    return;
  }

  (void)snprintf(text,
                 sizeof(text),
                 "[BOOT] metadata save %s status=%lu seq=%lu active=%lu switch=%lu upgrade=%lu crc=0x%08lX\r\n",
                 stage,
                 (unsigned long)status,
                 (unsigned long)metadata->sequence,
                 (unsigned long)metadata->active_slot,
                 (unsigned long)metadata->switch_slot,
                 (unsigned long)metadata->upgrade_flag,
                 (unsigned long)metadata->record_crc32);
  OtaMetadata_Log(text);
}

/** 计算元数据记录CRC */
static uint32_t OtaMetadata_CalculateRecordCrc(const OtaMetadata_TypeDef *metadata)
{
  return FlashIf_CalculateCrc32((const uint8_t *)metadata, offsetof(OtaMetadata_TypeDef, record_crc32));
}

/** 判断槽位号是否合法 */
static uint32_t OtaMetadata_IsSlotValid(uint32_t slot)
{
  return ((slot == OTA_SLOT_APP1) || (slot == OTA_SLOT_APP2)) ? 1U : 0U;
}

/** 生成默认元数据 */
void OtaMetadata_CreateDefault(OtaMetadata_TypeDef *metadata, uint32_t default_slot)
{
  if (metadata == NULL)
  {
    return;
  }

  memset(metadata, 0, sizeof(OtaMetadata_TypeDef));
  metadata->magic = OTA_METADATA_MAGIC;
  metadata->struct_version = OTA_METADATA_VERSION;
  metadata->sequence = 1U;
  metadata->active_slot = OtaMetadata_IsSlotValid(default_slot) ? default_slot : OTA_DEFAULT_APP_SLOT;
  metadata->switch_slot = OTA_SWITCH_FLAG_NONE;
  metadata->upgrade_flag = OTA_UPGRADE_FLAG_IDLE;
  metadata->last_error = OTA_STATUS_OK;
  metadata->record_crc32 = OtaMetadata_CalculateRecordCrc(metadata);
}

/** 从Flash加载元数据 */
uint32_t OtaMetadata_Load(OtaMetadata_TypeDef *metadata)
{
  const OtaMetadata_TypeDef *flash_metadata = (const OtaMetadata_TypeDef *)OTA_METADATA_ADDRESS;

  if (metadata == NULL)
  {
    return 0U;
  }

  memcpy(metadata, flash_metadata, sizeof(OtaMetadata_TypeDef));

  if (metadata->magic != OTA_METADATA_MAGIC)
  {
    OtaMetadata_Log("[BOOT] metadata load failed: magic mismatch\r\n");
    return 0U;
  }

  if (metadata->struct_version != OTA_METADATA_VERSION)
  {
    OtaMetadata_Log("[BOOT] metadata load failed: version mismatch\r\n");
    return 0U;
  }

  if (metadata->record_crc32 != OtaMetadata_CalculateRecordCrc(metadata))
  {
    OtaMetadata_Log("[BOOT] metadata load failed: crc mismatch\r\n");
    return 0U;
  }

  return 1U;
}

/** 保存元数据到Flash */
HAL_StatusTypeDef OtaMetadata_Save(OtaMetadata_TypeDef *metadata)
{
  HAL_StatusTypeDef status;

  if (metadata == NULL)
  {
    return HAL_ERROR;
  }

  metadata->magic = OTA_METADATA_MAGIC;
  metadata->struct_version = OTA_METADATA_VERSION;
  metadata->sequence += 1U;
  metadata->record_crc32 = OtaMetadata_CalculateRecordCrc(metadata);

  OtaMetadata_LogSaveState(metadata, HAL_OK, "prepare");
  status = FlashIf_EraseMetadata();
  if (status != HAL_OK)
  {
    OtaMetadata_LogSaveState(metadata, status, "erase_failed");
    return status;
  }

  status = FlashIf_WriteBuffer(OTA_METADATA_ADDRESS, (const uint8_t *)metadata, sizeof(OtaMetadata_TypeDef));
  OtaMetadata_LogSaveState(metadata, status, "write_done");
  return status;
}

/** 选择本次应启动的槽位 */
uint32_t OtaMetadata_SelectBootSlot(const OtaMetadata_TypeDef *metadata, uint32_t default_slot)
{
  if (metadata == NULL)
  {
    return default_slot;
  }

  if (OtaMetadata_IsSlotValid(metadata->switch_slot) != 0U)
  {
    return metadata->switch_slot;
  }

  if (OtaMetadata_IsSlotValid(metadata->active_slot) != 0U)
  {
    return metadata->active_slot;
  }

  return default_slot;
}

/** 判断槽位是否已经被标记为有效 */
uint32_t OtaMetadata_IsSlotMarkedValid(const OtaMetadata_TypeDef *metadata, uint32_t slot)
{
  if (metadata == NULL)
  {
    return 0U;
  }

  if (slot == OTA_SLOT_APP1)
  {
    return metadata->app1_valid;
  }

  if (slot == OTA_SLOT_APP2)
  {
    return metadata->app2_valid;
  }

  return 0U;
}

/** 更新指定槽位镜像信息 */
void OtaMetadata_UpdateSlotInfo(OtaMetadata_TypeDef *metadata,
                                uint32_t slot,
                                uint32_t image_size,
                                uint32_t image_crc32,
                                uint32_t image_version,
                                uint32_t is_valid)
{
  if (metadata == NULL)
  {
    return;
  }

  if (slot == OTA_SLOT_APP1)
  {
    metadata->app1_size = image_size;
    metadata->app1_crc32 = image_crc32;
    metadata->app1_version = image_version;
    metadata->app1_valid = is_valid;
  }
  else if (slot == OTA_SLOT_APP2)
  {
    metadata->app2_size = image_size;
    metadata->app2_crc32 = image_crc32;
    metadata->app2_version = image_version;
    metadata->app2_valid = is_valid;
  }
}
