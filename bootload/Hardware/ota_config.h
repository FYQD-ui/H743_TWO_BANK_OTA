#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

#include "main.h"

/** OTA默认启动槽位 */
#define OTA_DEFAULT_APP_SLOT            1U

/** APP1槽位编号 */
#define OTA_SLOT_APP1                   1U
/** APP2槽位编号 */
#define OTA_SLOT_APP2                   2U
/** 无效槽位编号 */
#define OTA_SLOT_NONE                   0U

/** APP1起始地址 */
#define OTA_APP1_ADDRESS                0x08020000U
/** APP1可用大小，单位字节 */
#define OTA_APP1_SIZE                   0x000E0000U
/** APP2起始地址 */
#define OTA_APP2_ADDRESS                0x08100000U
/** APP2可用大小，单位字节 */
#define OTA_APP2_SIZE                   0x000E0000U

/** 元数据区起始地址 */
#define OTA_METADATA_ADDRESS            0x081E0000U
/** 元数据区大小，单位字节 */
#define OTA_METADATA_SIZE               0x00020000U
/** 元数据区所在Bank */
#define OTA_METADATA_BANK               FLASH_BANK_2
/** 元数据区所在Sector */
#define OTA_METADATA_SECTOR             FLASH_SECTOR_7

/** 升级标志，空闲状态 */
#define OTA_UPGRADE_FLAG_IDLE           0U
/** 升级标志，请求进入升级状态 */
#define OTA_UPGRADE_FLAG_REQUEST        1U
/** 升级标志，升级进行中 */
#define OTA_UPGRADE_FLAG_ACTIVE         2U

/** 切换标志，无切换请求 */
#define OTA_SWITCH_FLAG_NONE            OTA_SLOT_NONE
/** 切换标志，请求切换到APP1 */
#define OTA_SWITCH_FLAG_APP1            OTA_SLOT_APP1
/** 切换标志，请求切换到APP2 */
#define OTA_SWITCH_FLAG_APP2            OTA_SLOT_APP2

/** 元数据魔术字 */
#define OTA_METADATA_MAGIC              0x4F54414DU
/** 元数据版本号 */
#define OTA_METADATA_VERSION            0x00010000U

/** OTA协议魔术字，字符为OTA1 */
#define OTA_PROTOCOL_MAGIC              0x3141544FU
/** OTA握手等待时间，单位毫秒 */
#define OTA_PROTOCOL_HANDSHAKE_TIMEOUT  1500U
/** OTA单帧接收超时时间，单位毫秒 */
#define OTA_PROTOCOL_FRAME_TIMEOUT      5000U
/** OTA单包最大载荷长度 */
#define OTA_PROTOCOL_MAX_DATA_LENGTH    256U

/** OTA协议命令：握手 */
#define OTA_CMD_HELLO                   0x0001U
/** OTA协议命令：开始升级 */
#define OTA_CMD_START                   0x0002U
/** OTA协议命令：数据包 */
#define OTA_CMD_DATA                    0x0003U
/** OTA协议命令：结束升级 */
#define OTA_CMD_FINISH                  0x0004U
/** OTA协议命令：中止升级 */
#define OTA_CMD_ABORT                   0x0005U
/** OTA协议命令：查询信息 */
#define OTA_CMD_QUERY                   0x0006U

/** OTA响应状态：成功 */
#define OTA_STATUS_OK                   0x00000000U
/** OTA响应状态：超时 */
#define OTA_STATUS_TIMEOUT              0x00000001U
/** OTA响应状态：帧魔术字错误 */
#define OTA_STATUS_MAGIC_ERROR          0x00000002U
/** OTA响应状态：帧CRC错误 */
#define OTA_STATUS_CRC_ERROR            0x00000003U
/** OTA响应状态：命令状态错误 */
#define OTA_STATUS_STATE_ERROR          0x00000004U
/** OTA响应状态：目标槽位错误 */
#define OTA_STATUS_SLOT_ERROR           0x00000005U
/** OTA响应状态：镜像大小错误 */
#define OTA_STATUS_SIZE_ERROR           0x00000006U
/** OTA响应状态：Flash操作失败 */
#define OTA_STATUS_FLASH_ERROR          0x00000007U
/** OTA响应状态：镜像校验失败 */
#define OTA_STATUS_VERIFY_ERROR         0x00000008U
/** OTA响应状态：参数错误 */
#define OTA_STATUS_ARGUMENT_ERROR       0x00000009U

/** OTA运行结果：未进入升级模式 */
#define OTA_RUN_RESULT_NO_SESSION       0U
/** OTA运行结果：升级完成，需要复位 */
#define OTA_RUN_RESULT_UPDATE_DONE      1U
/** OTA运行结果：升级中止或失败 */
#define OTA_RUN_RESULT_STAY_BOOT        2U

#endif
