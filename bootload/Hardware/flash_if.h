#ifndef FLASH_IF_H
#define FLASH_IF_H

#include "ota_config.h"

/** 查询槽位对应的起始地址 */
uint32_t FlashIf_GetSlotAddress(uint32_t slot);

/** 查询槽位对应的可用大小 */
uint32_t FlashIf_GetSlotSize(uint32_t slot);

/** 擦除指定槽位的全部区域 */
HAL_StatusTypeDef FlashIf_EraseSlot(uint32_t slot);

/** 擦除元数据区域 */
HAL_StatusTypeDef FlashIf_EraseMetadata(void);

/** 向指定地址写入任意长度数据 */
HAL_StatusTypeDef FlashIf_WriteBuffer(uint32_t address, const uint8_t *buffer, uint32_t length);

/** 从Flash区域计算CRC32 */
uint32_t FlashIf_CalculateCrc32(const uint8_t *buffer, uint32_t length);

/** 对Flash地址范围计算CRC32 */
uint32_t FlashIf_CalculateFlashCrc32(uint32_t address, uint32_t length);

/** 检查应用向量表是否有效 */
uint32_t FlashIf_IsApplicationValid(uint32_t app_address, uint32_t app_size);

#endif
