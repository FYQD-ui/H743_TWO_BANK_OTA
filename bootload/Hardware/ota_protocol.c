#include "ota_protocol.h"
#include "flash_if.h"
#include "uart_if.h"
#include <string.h>

/** OTA会话上下文 */
typedef struct
{
  uint32_t started;
  uint32_t target_slot;
  uint32_t image_address;
  uint32_t slot_size;
  uint32_t expected_size;
  uint32_t expected_crc32;
  uint32_t image_version;
  uint32_t received_size;
} OtaProtocol_ContextTypeDef;

/** 发送协议响应帧 */
static HAL_StatusTypeDef OtaProtocol_SendResponse(uint16_t command,
                                                  uint32_t status,
                                                  uint32_t value0,
                                                  uint32_t value1,
                                                  uint32_t value2)
{
  OtaProtocol_FrameHeaderTypeDef header = {0};
  OtaProtocol_ResponsePayloadTypeDef payload = {0};

  payload.status = status;
  payload.value0 = value0;
  payload.value1 = value1;
  payload.value2 = value2;

  header.magic = OTA_PROTOCOL_MAGIC;
  header.command = (uint16_t)(command | 0x8000U);
  header.length = (uint16_t)sizeof(payload);
  header.sequence = 0U;
  header.payload_crc32 = FlashIf_CalculateCrc32((const uint8_t *)&payload, sizeof(payload));

  if (UartIf_SendBuffer((const uint8_t *)&header, (uint16_t)sizeof(header)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return UartIf_SendBuffer((const uint8_t *)&payload, (uint16_t)sizeof(payload));
}

/** 接收完整的一帧协议数据 */
static HAL_StatusTypeDef OtaProtocol_ReceiveFrame(OtaProtocol_FrameHeaderTypeDef *header,
                                                  uint8_t *payload,
                                                  uint32_t timeout)
{
  HAL_StatusTypeDef status;

  status = UartIf_ReceiveBuffer((uint8_t *)header, (uint16_t)sizeof(OtaProtocol_FrameHeaderTypeDef), timeout);
  if (status != HAL_OK)
  {
    return status;
  }

  if (header->magic != OTA_PROTOCOL_MAGIC)
  {
    return HAL_ERROR;
  }

  if (header->length > OTA_PROTOCOL_MAX_DATA_LENGTH)
  {
    return HAL_ERROR;
  }

  if (header->length > 0U)
  {
    status = UartIf_ReceiveBuffer(payload, header->length, timeout);
    if (status != HAL_OK)
    {
      return status;
    }

    if (header->payload_crc32 != FlashIf_CalculateCrc32(payload, header->length))
    {
      return HAL_ERROR;
    }
  }

  return HAL_OK;
}

/** 发送当前元数据信息 */
static void OtaProtocol_SendQueryInfo(const OtaMetadata_TypeDef *metadata)
{
  uint32_t app1_info = 0U;
  uint32_t app2_info = 0U;

  if (metadata != NULL)
  {
    app1_info = (metadata->app1_valid & 0xFFFFU) | ((metadata->active_slot & 0xFFFFU) << 16U);
    app2_info = (metadata->app2_valid & 0xFFFFU) | ((metadata->switch_slot & 0xFFFFU) << 16U);
    (void)OtaProtocol_SendResponse(OTA_CMD_QUERY, OTA_STATUS_OK, metadata->upgrade_flag, app1_info, app2_info);
  }
  else
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_QUERY, OTA_STATUS_ARGUMENT_ERROR, 0U, 0U, 0U);
  }
}

/** 处理开始升级命令 */
static uint32_t OtaProtocol_HandleStart(OtaProtocol_ContextTypeDef *context,
                                        OtaMetadata_TypeDef *metadata,
                                        const uint8_t *payload,
                                        uint16_t length)
{
  OtaProtocol_StartPayloadTypeDef start_info;

  if ((context == NULL) || (metadata == NULL) || (payload == NULL))
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_START, OTA_STATUS_ARGUMENT_ERROR, 0U, 0U, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  if ((context->started != 0U) || (length != sizeof(OtaProtocol_StartPayloadTypeDef)))
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_START, OTA_STATUS_STATE_ERROR, 0U, 0U, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  memcpy(&start_info, payload, sizeof(start_info));

  if ((start_info.target_slot != OTA_SLOT_APP1) && (start_info.target_slot != OTA_SLOT_APP2))
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_START, OTA_STATUS_SLOT_ERROR, start_info.target_slot, 0U, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  if ((metadata->active_slot == start_info.target_slot) && (metadata->active_slot != OTA_SLOT_NONE))
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_START, OTA_STATUS_SLOT_ERROR, start_info.target_slot, metadata->active_slot, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  context->slot_size = FlashIf_GetSlotSize(start_info.target_slot);
  if ((start_info.image_size == 0U) || (start_info.image_size > context->slot_size))
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_START, OTA_STATUS_SIZE_ERROR, start_info.image_size, context->slot_size, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  context->target_slot = start_info.target_slot;
  context->image_address = FlashIf_GetSlotAddress(start_info.target_slot);
  context->expected_size = start_info.image_size;
  context->expected_crc32 = start_info.image_crc32;
  context->image_version = start_info.image_version;
  context->received_size = 0U;

  UartIf_SendString("[BOOT] erase target slot\r\n");
  if (FlashIf_EraseSlot(context->target_slot) != HAL_OK)
  {
    metadata->last_error = OTA_STATUS_FLASH_ERROR;
    metadata->upgrade_flag = OTA_UPGRADE_FLAG_IDLE;
    (void)OtaMetadata_Save(metadata);
    (void)OtaProtocol_SendResponse(OTA_CMD_START, OTA_STATUS_FLASH_ERROR, context->target_slot, 0U, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  metadata->upgrade_flag = OTA_UPGRADE_FLAG_ACTIVE;
  metadata->last_error = OTA_STATUS_OK;
  OtaMetadata_UpdateSlotInfo(metadata, context->target_slot, 0U, 0U, context->image_version, 0U);
  (void)OtaMetadata_Save(metadata);

  context->started = 1U;
  (void)OtaProtocol_SendResponse(OTA_CMD_START, OTA_STATUS_OK, context->target_slot, context->expected_size, context->image_version);
  return OTA_RUN_RESULT_STAY_BOOT;
}

/** 处理数据包命令 */
static uint32_t OtaProtocol_HandleData(OtaProtocol_ContextTypeDef *context,
                                       OtaMetadata_TypeDef *metadata,
                                       const uint8_t *payload,
                                       uint16_t length)
{
  HAL_StatusTypeDef status;

  if ((context == NULL) || (metadata == NULL) || (payload == NULL))
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_DATA, OTA_STATUS_ARGUMENT_ERROR, 0U, 0U, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  if ((context->started == 0U) || (length == 0U))
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_DATA, OTA_STATUS_STATE_ERROR, 0U, 0U, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  if ((context->received_size + length) > context->expected_size)
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_DATA, OTA_STATUS_SIZE_ERROR, context->received_size, context->expected_size, length);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  status = FlashIf_WriteBuffer(context->image_address + context->received_size, payload, length);
  if (status != HAL_OK)
  {
    metadata->last_error = OTA_STATUS_FLASH_ERROR;
    metadata->upgrade_flag = OTA_UPGRADE_FLAG_IDLE;
    (void)OtaMetadata_Save(metadata);
    (void)OtaProtocol_SendResponse(OTA_CMD_DATA, OTA_STATUS_FLASH_ERROR, context->received_size, length, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  context->received_size += length;
  (void)OtaProtocol_SendResponse(OTA_CMD_DATA, OTA_STATUS_OK, context->received_size, context->expected_size, 0U);
  return OTA_RUN_RESULT_STAY_BOOT;
}

/** 处理结束升级命令 */
static uint32_t OtaProtocol_HandleFinish(OtaProtocol_ContextTypeDef *context, OtaMetadata_TypeDef *metadata)
{
  uint32_t image_crc32;

  if ((context == NULL) || (metadata == NULL))
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_FINISH, OTA_STATUS_ARGUMENT_ERROR, 0U, 0U, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  if ((context->started == 0U) || (context->received_size != context->expected_size))
  {
    metadata->last_error = OTA_STATUS_SIZE_ERROR;
    metadata->upgrade_flag = OTA_UPGRADE_FLAG_IDLE;
    (void)OtaMetadata_Save(metadata);
    (void)OtaProtocol_SendResponse(OTA_CMD_FINISH, OTA_STATUS_SIZE_ERROR, context->received_size, context->expected_size, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  UartIf_SendString("[BOOT] verify image crc\r\n");
  image_crc32 = FlashIf_CalculateFlashCrc32(context->image_address, context->expected_size);
  if (image_crc32 != context->expected_crc32)
  {
    metadata->last_error = OTA_STATUS_VERIFY_ERROR;
    metadata->upgrade_flag = OTA_UPGRADE_FLAG_IDLE;
    OtaMetadata_UpdateSlotInfo(metadata, context->target_slot, 0U, 0U, context->image_version, 0U);
    (void)OtaMetadata_Save(metadata);
    (void)OtaProtocol_SendResponse(OTA_CMD_FINISH, OTA_STATUS_VERIFY_ERROR, image_crc32, context->expected_crc32, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  OtaMetadata_UpdateSlotInfo(metadata,
                             context->target_slot,
                             context->expected_size,
                             context->expected_crc32,
                             context->image_version,
                             1U);
  metadata->switch_slot = context->target_slot;
  metadata->upgrade_flag = OTA_UPGRADE_FLAG_IDLE;
  metadata->last_error = OTA_STATUS_OK;
  if (OtaMetadata_Save(metadata) != HAL_OK)
  {
    (void)OtaProtocol_SendResponse(OTA_CMD_FINISH, OTA_STATUS_FLASH_ERROR, 0U, 0U, 0U);
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  (void)OtaProtocol_SendResponse(OTA_CMD_FINISH, OTA_STATUS_OK, context->target_slot, context->expected_size, context->expected_crc32);
  UartIf_SendString("[BOOT] ota update done, reset required\r\n");
  return OTA_RUN_RESULT_UPDATE_DONE;
}

/** 运行OTA会话 */
uint32_t OtaProtocol_RunSession(OtaMetadata_TypeDef *metadata, uint32_t wait_forever)
{
  OtaProtocol_ContextTypeDef context = {0};
  OtaProtocol_FrameHeaderTypeDef header = {0};
  uint8_t payload[OTA_PROTOCOL_MAX_DATA_LENGTH] = {0};
  HAL_StatusTypeDef status;
  uint32_t timeout;
  uint32_t handshake_done = 0U;

  if (metadata == NULL)
  {
    return OTA_RUN_RESULT_STAY_BOOT;
  }

  UartIf_SendString("[BOOT] wait ota handshake\r\n");

  while (1)
  {
    timeout = (wait_forever != 0U) ? HAL_MAX_DELAY : OTA_PROTOCOL_HANDSHAKE_TIMEOUT;
    status = OtaProtocol_ReceiveFrame(&header, payload, timeout);
    if (status != HAL_OK)
    {
      return (handshake_done == 0U) ? OTA_RUN_RESULT_NO_SESSION : OTA_RUN_RESULT_STAY_BOOT;
    }

    if (handshake_done == 0U)
    {
      if (header.command != OTA_CMD_HELLO)
      {
        (void)OtaProtocol_SendResponse(header.command, OTA_STATUS_STATE_ERROR, 0U, 0U, 0U);
        return OTA_RUN_RESULT_STAY_BOOT;
      }

      handshake_done = 1U;
      UartIf_SendString("[BOOT] ota session opened\r\n");
      (void)OtaProtocol_SendResponse(OTA_CMD_HELLO,
                                     OTA_STATUS_OK,
                                     metadata->active_slot,
                                     metadata->app1_valid,
                                     metadata->app2_valid);
      continue;
    }

    switch (header.command)
    {
      case OTA_CMD_QUERY:
        OtaProtocol_SendQueryInfo(metadata);
        break;

      case OTA_CMD_START:
        if (OtaProtocol_HandleStart(&context, metadata, payload, header.length) == OTA_RUN_RESULT_UPDATE_DONE)
        {
          return OTA_RUN_RESULT_UPDATE_DONE;
        }
        break;

      case OTA_CMD_DATA:
        if (OtaProtocol_HandleData(&context, metadata, payload, header.length) == OTA_RUN_RESULT_UPDATE_DONE)
        {
          return OTA_RUN_RESULT_UPDATE_DONE;
        }
        break;

      case OTA_CMD_FINISH:
        return OtaProtocol_HandleFinish(&context, metadata);

      case OTA_CMD_ABORT:
        metadata->upgrade_flag = OTA_UPGRADE_FLAG_IDLE;
        metadata->last_error = OTA_STATUS_STATE_ERROR;
        (void)OtaMetadata_Save(metadata);
        (void)OtaProtocol_SendResponse(OTA_CMD_ABORT, OTA_STATUS_OK, 0U, 0U, 0U);
        UartIf_SendString("[BOOT] ota session aborted\r\n");
        return OTA_RUN_RESULT_STAY_BOOT;

      default:
        (void)OtaProtocol_SendResponse(header.command, OTA_STATUS_ARGUMENT_ERROR, header.command, 0U, 0U);
        break;
    }
  }
}
