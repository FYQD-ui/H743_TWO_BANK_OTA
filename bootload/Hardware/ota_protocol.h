#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

#include "ota_metadata.h"

/** OTA帧头结构 */
typedef __PACKED_STRUCT
{
  uint32_t magic;
  uint16_t command;
  uint16_t length;
  uint32_t sequence;
  uint32_t payload_crc32;
} OtaProtocol_FrameHeaderTypeDef;

/** OTA开始升级命令载荷 */
typedef __PACKED_STRUCT
{
  uint32_t target_slot;
  uint32_t image_size;
  uint32_t image_crc32;
  uint32_t image_version;
} OtaProtocol_StartPayloadTypeDef;

/** OTA响应载荷 */
typedef __PACKED_STRUCT
{
  uint32_t status;
  uint32_t value0;
  uint32_t value1;
  uint32_t value2;
} OtaProtocol_ResponsePayloadTypeDef;

/** 运行OTA会话 */
uint32_t OtaProtocol_RunSession(OtaMetadata_TypeDef *metadata, uint32_t wait_forever);

#endif
