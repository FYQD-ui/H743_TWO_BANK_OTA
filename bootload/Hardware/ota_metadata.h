#ifndef OTA_METADATA_H
#define OTA_METADATA_H

#include "ota_config.h"

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
} OtaMetadata_TypeDef;

/** 从Flash加载元数据 */
uint32_t OtaMetadata_Load(OtaMetadata_TypeDef *metadata);

/** 生成默认元数据 */
void OtaMetadata_CreateDefault(OtaMetadata_TypeDef *metadata, uint32_t default_slot);

/** 保存元数据到Flash */
HAL_StatusTypeDef OtaMetadata_Save(OtaMetadata_TypeDef *metadata);

/** 选择本次应启动的槽位 */
uint32_t OtaMetadata_SelectBootSlot(const OtaMetadata_TypeDef *metadata, uint32_t default_slot);

/** 判断槽位是否已经被标记为有效 */
uint32_t OtaMetadata_IsSlotMarkedValid(const OtaMetadata_TypeDef *metadata, uint32_t slot);

/** 更新指定槽位镜像信息 */
void OtaMetadata_UpdateSlotInfo(OtaMetadata_TypeDef *metadata,
                                uint32_t slot,
                                uint32_t image_size,
                                uint32_t image_crc32,
                                uint32_t image_version,
                                uint32_t is_valid);

#endif
