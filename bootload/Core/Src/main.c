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
  * If no LICENSE file comes with this software, it is provided AS-IS.
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
#include "flash_if.h"
#include "ota_config.h"
#include "ota_metadata.h"
#include "ota_protocol.h"
#include "uart_if.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/** 閹煎瓨姊婚弫銈夊礂閵夈儱缍撻柛鎴ｅГ閺嗙喓鐚剧拠鑼偓?*/
typedef void (*application_entry_t)(void);
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
/** 鏉堟挸鍤瑽oot鐠嬪啳鐦弮銉ョ箶 */
static void Boot_Log(const char *text);
/** 閺屻儴顕楀Σ鎴掔秴閸氬秶袨鐎涙顑佹稉?*/
static const char *Boot_GetSlotName(uint32_t slot);
/** 鏉堟挸鍤弽鍥╊劮娑撳骸宕勯崗顓＄箻閸掕埖鏆熼崐?*/
static void Boot_LogHexValue(const char *label, uint32_t value);
/** 鏉堟挸鍤崡鏇氶嚋濡叉垝缍呴惃鍕値濞夋洘鈧呭Ц閹?*/
static void Boot_LogSlotState(const OtaMetadata_TypeDef *metadata, uint32_t slot);
/** 鏉堟挸鍤稉銈勯嚋鎼存梻鏁ゅΣ鎴掔秴閻ㄥ嫬鐣弫瀵稿Ц閹?*/
static void Boot_LogImageSummary(const OtaMetadata_TypeDef *metadata);
/** 鏉堟挸鍤ぐ鎾冲閸忓啯鏆熼幑顔惧Ц閹?*/
static void Boot_LogMetadataState(const OtaMetadata_TypeDef *metadata);
/** 閸掋倖鏌囧Σ鎴掔秴閺勵垰鎯侀崣顖氭儙閸?*/
static uint32_t Boot_IsSlotBootable(const OtaMetadata_TypeDef *metadata, uint32_t slot);
/** 閼惧嘲褰囬崣锔跨娑擃亜绨查悽銊π担?*/
static uint32_t Boot_GetAlternateSlot(uint32_t slot);
/** 閸掓繂顫愰崠鏍帛鐠併倕鍘撻弫鐗堝祦楠炶泛鎮撳銉ュ嚒閺堝鏆呴崓蹇曞Ц閹?*/
static void Boot_InitMetadataDefaults(OtaMetadata_TypeDef *metadata);
/** 閺嶈宓佺€圭偤妾梹婊冨剼缂佹挻鐏夊〒鍛倞閺冪姵鏅ュΣ鎴掔秴閺嶅洩顔?*/
static void Boot_SanitizeMetadata(OtaMetadata_TypeDef *metadata);
/** 婢跺嫮鎮婂鍛瀼閹广垺蝎娴ｅ秴鑻熼崚閿嬫煀瑜版挸澧犲ú璇插З濡叉垝缍?*/
static void Boot_ApplyPendingSwitch(OtaMetadata_TypeDef *metadata);
/** 闁瀚ㄨぐ鎾冲鎼存柨鎯庨崝銊ф畱閻╊喗鐖ｅΣ鎴掔秴 */
static uint32_t Boot_SelectTargetSlot(OtaMetadata_TypeDef *metadata);
/** 鐠哄疇娴嗛崜宥嗗⒔鐞涘苯顦荤拋鎯у冀閸掓繂顫愰崠?*/
static void Boot_DeInitBeforeJump(void);
/** 鐠哄疇娴嗛崚鐗堝瘹鐎规艾绨查悽銊ユ勾閸р偓閹笛嗩攽 */
static void Boot_JumpToApplication(uint32_t app_address);
/** 鏉╂稑鍙咮oot鐢悂鈹楅幁銏狀槻濡€崇础 */
static void Boot_EnterRecoveryMode(OtaMetadata_TypeDef *metadata);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/** 鏉堟挸鍤瑽oot鐠嬪啳鐦弮銉ョ箶 */
static void Boot_Log(const char *text)
{
  (void)UartIf_SendString(text);
}

/** 閺屻儴顕楀Σ鎴掔秴閸氬秶袨鐎涙顑佹稉?*/
static const char *Boot_GetSlotName(uint32_t slot)
{
  if (slot == OTA_SLOT_APP1)
  {
    return "APP1";
  }

  if (slot == OTA_SLOT_APP2)
  {
    return "APP2";
  }

  return "NONE";
}

/** 鏉堟挸鍤弽鍥╊劮娑撳骸宕勯崗顓＄箻閸掕埖鏆熼崐?*/
static void Boot_LogHexValue(const char *label, uint32_t value)
{
  char text[80];

  (void)snprintf(text, sizeof(text), "%s0x%08lX\r\n", label, (unsigned long)value);
  Boot_Log(text);
}

/** 鏉堟挸鍤崡鏇氶嚋濡叉垝缍呴惃鍕値濞夋洘鈧呭Ц閹?*/
static void Boot_LogSlotState(const OtaMetadata_TypeDef *metadata, uint32_t slot)
{
  char text[160];
  uint32_t image_valid;
  uint32_t metadata_valid = 0U;
  uint32_t slot_address;
  uint32_t slot_size;

  slot_address = FlashIf_GetSlotAddress(slot);
  slot_size = FlashIf_GetSlotSize(slot);
  image_valid = FlashIf_IsApplicationValid(slot_address, slot_size);
  if (metadata != NULL)
  {
    metadata_valid = OtaMetadata_IsSlotMarkedValid(metadata, slot);
  }

  (void)snprintf(text,
                 sizeof(text),
                 "[BOOT] %s image_valid=%lu metadata_valid=%lu addr=0x%08lX size=0x%08lX\r\n",
                 Boot_GetSlotName(slot),
                 (unsigned long)image_valid,
                 (unsigned long)metadata_valid,
                 (unsigned long)slot_address,
                 (unsigned long)slot_size);
  Boot_Log(text);
}

/** 鏉堟挸鍤稉銈勯嚋鎼存梻鏁ゅΣ鎴掔秴閻ㄥ嫬鐣弫瀵稿Ц閹?*/
static void Boot_LogImageSummary(const OtaMetadata_TypeDef *metadata)
{
  Boot_LogSlotState(metadata, OTA_SLOT_APP1);
  Boot_LogSlotState(metadata, OTA_SLOT_APP2);
}

/** 鏉堟挸鍤ぐ鎾冲閸忓啯鏆熼幑顔惧Ц閹?*/
static void Boot_LogMetadataState(const OtaMetadata_TypeDef *metadata)
{
  char text[192];

  if (metadata == NULL)
  {
    Boot_Log("[BOOT] metadata pointer is null\r\n");
    return;
  }

  (void)snprintf(text,
                 sizeof(text),
                 "[BOOT] metadata seq=%lu active=%s(%lu) switch=%s(%lu) upgrade=%lu last_error=0x%08lX\r\n",
                 (unsigned long)metadata->sequence,
                 Boot_GetSlotName(metadata->active_slot),
                 (unsigned long)metadata->active_slot,
                 Boot_GetSlotName(metadata->switch_slot),
                 (unsigned long)metadata->switch_slot,
                 (unsigned long)metadata->upgrade_flag,
                 (unsigned long)metadata->last_error);
  Boot_Log(text);

  (void)snprintf(text,
                 sizeof(text),
                 "[BOOT] metadata app1_valid=%lu app2_valid=%lu app1_size=0x%08lX app2_size=0x%08lX\r\n",
                 (unsigned long)metadata->app1_valid,
                 (unsigned long)metadata->app2_valid,
                 (unsigned long)metadata->app1_size,
                 (unsigned long)metadata->app2_size);
  Boot_Log(text);
}

/** 閸掋倖鏌囧Σ鎴掔秴閺勵垰鎯侀崣顖氭儙閸?*/
static uint32_t Boot_IsSlotBootable(const OtaMetadata_TypeDef *metadata, uint32_t slot)
{
  uint32_t slot_address;
  uint32_t slot_size;

  if ((metadata == NULL) || (OtaMetadata_IsSlotMarkedValid(metadata, slot) == 0U))
  {
    return 0U;
  }

  slot_address = FlashIf_GetSlotAddress(slot);
  slot_size = FlashIf_GetSlotSize(slot);
  if ((slot_address == 0U) || (slot_size == 0U))
  {
    return 0U;
  }

  return FlashIf_IsApplicationValid(slot_address, slot_size);
}

/** 閼惧嘲褰囬崣锔跨娑擃亜绨查悽銊π担?*/
static uint32_t Boot_GetAlternateSlot(uint32_t slot)
{
  if (slot == OTA_SLOT_APP1)
  {
    return OTA_SLOT_APP2;
  }

  if (slot == OTA_SLOT_APP2)
  {
    return OTA_SLOT_APP1;
  }

  return OTA_SLOT_NONE;
}

/** 閸掓繂顫愰崠鏍帛鐠併倕鍘撻弫鐗堝祦楠炶泛鎮撳銉ュ嚒閺堝鏆呴崓蹇曞Ц閹?*/
static void Boot_InitMetadataDefaults(OtaMetadata_TypeDef *metadata)
{
  uint32_t app1_valid;
  uint32_t app2_valid;

  if (metadata == NULL)
  {
    return;
  }

  OtaMetadata_CreateDefault(metadata, OTA_DEFAULT_APP_SLOT);

  app1_valid = FlashIf_IsApplicationValid(OTA_APP1_ADDRESS, OTA_APP1_SIZE);
  app2_valid = FlashIf_IsApplicationValid(OTA_APP2_ADDRESS, OTA_APP2_SIZE);

  OtaMetadata_UpdateSlotInfo(metadata, OTA_SLOT_APP1, 0U, 0U, 0U, app1_valid);
  OtaMetadata_UpdateSlotInfo(metadata, OTA_SLOT_APP2, 0U, 0U, 0U, app2_valid);

  if ((app1_valid == 0U) && (app2_valid != 0U))
  {
    metadata->active_slot = OTA_SLOT_APP2;
  }
  else
  {
    metadata->active_slot = OTA_SLOT_APP1;
  }

  Boot_Log("[BOOT] default metadata select active slot: ");
  Boot_Log(Boot_GetSlotName(metadata->active_slot));
  Boot_Log("\r\n");
  Boot_LogHexValue("[BOOT] default metadata app1_valid: ", app1_valid);
  Boot_LogHexValue("[BOOT] default metadata app2_valid: ", app2_valid);

  metadata->switch_slot = OTA_SWITCH_FLAG_NONE;
  metadata->upgrade_flag = OTA_UPGRADE_FLAG_IDLE;
  metadata->last_error = OTA_STATUS_OK;

  if (OtaMetadata_Save(metadata) == HAL_OK)
  {
    Boot_Log("[BOOT] ota metadata created\r\n");
  }
  else
  {
    Boot_Log("[BOOT] ota metadata save failed\r\n");
  }
}

/** 閺嶈宓佺€圭偤妾梹婊冨剼缂佹挻鐏夊〒鍛倞閺冪姵鏅ュΣ鎴掔秴閺嶅洩顔?*/
static void Boot_SanitizeMetadata(OtaMetadata_TypeDef *metadata)
{
  uint32_t metadata_changed = 0U;

  if (metadata == NULL)
  {
    return;
  }

  if ((metadata->app1_valid != 0U) && (FlashIf_IsApplicationValid(OTA_APP1_ADDRESS, OTA_APP1_SIZE) == 0U))
  {
    OtaMetadata_UpdateSlotInfo(metadata, OTA_SLOT_APP1, 0U, 0U, 0U, 0U);
    metadata_changed = 1U;
  }

  if ((metadata->app2_valid != 0U) && (FlashIf_IsApplicationValid(OTA_APP2_ADDRESS, OTA_APP2_SIZE) == 0U))
  {
    OtaMetadata_UpdateSlotInfo(metadata, OTA_SLOT_APP2, 0U, 0U, 0U, 0U);
    metadata_changed = 1U;
  }

  if ((metadata->active_slot != OTA_SLOT_NONE) && (Boot_IsSlotBootable(metadata, metadata->active_slot) == 0U))
  {
    metadata->active_slot = OTA_SLOT_NONE;
    metadata_changed = 1U;
  }

  if ((metadata->switch_slot != OTA_SWITCH_FLAG_NONE) && (Boot_IsSlotBootable(metadata, metadata->switch_slot) == 0U))
  {
    metadata->switch_slot = OTA_SWITCH_FLAG_NONE;
    metadata_changed = 1U;
  }

  if (metadata_changed != 0U)
  {
    (void)OtaMetadata_Save(metadata);
  }
}

/** 婢跺嫮鎮婂鍛瀼閹广垺蝎娴ｅ秴鑻熼崚閿嬫煀瑜版挸澧犲ú璇插З濡叉垝缍?*/
static void Boot_ApplyPendingSwitch(OtaMetadata_TypeDef *metadata)
{
  if (metadata == NULL)
  {
    return;
  }

  if (metadata->switch_slot == OTA_SWITCH_FLAG_NONE)
  {
    return;
  }

  if (Boot_IsSlotBootable(metadata, metadata->switch_slot) != 0U)
  {
    Boot_Log("[BOOT] apply switch slot: ");
    Boot_Log(Boot_GetSlotName(metadata->switch_slot));
    Boot_Log("\r\n");
    metadata->active_slot = metadata->switch_slot;
  }
  else
  {
    Boot_Log("[BOOT] pending switch slot invalid\r\n");
    metadata->last_error = OTA_STATUS_VERIFY_ERROR;
  }

  metadata->switch_slot = OTA_SWITCH_FLAG_NONE;
  (void)OtaMetadata_Save(metadata);
}

/** 闁瀚ㄨぐ鎾冲鎼存柨鎯庨崝銊ф畱閻╊喗鐖ｅΣ鎴掔秴 */
static uint32_t Boot_SelectTargetSlot(OtaMetadata_TypeDef *metadata)
{
  uint32_t preferred_slot;
  uint32_t alternate_slot;

  if (metadata == NULL)
  {
    return OTA_SLOT_NONE;
  }

  preferred_slot = OtaMetadata_SelectBootSlot(metadata, OTA_DEFAULT_APP_SLOT);
  Boot_Log("[BOOT] preferred slot: ");
  Boot_Log(Boot_GetSlotName(preferred_slot));
  Boot_Log("\r\n");
  if (Boot_IsSlotBootable(metadata, preferred_slot) != 0U)
  {
    Boot_Log("[BOOT] preferred slot is bootable\r\n");
    return preferred_slot;
  }

  Boot_Log("[BOOT] preferred slot is not bootable, try alternate\r\n");
  alternate_slot = Boot_GetAlternateSlot(preferred_slot);
  Boot_Log("[BOOT] alternate slot: ");
  Boot_Log(Boot_GetSlotName(alternate_slot));
  Boot_Log("\r\n");
  if (Boot_IsSlotBootable(metadata, alternate_slot) != 0U)
  {
    Boot_Log("[BOOT] alternate slot is bootable\r\n");
    metadata->active_slot = alternate_slot;
    metadata->switch_slot = OTA_SWITCH_FLAG_NONE;
    (void)OtaMetadata_Save(metadata);
    return alternate_slot;
  }

  Boot_Log("[BOOT] alternate slot is also not bootable\r\n");
  return OTA_SLOT_NONE;
}

/** 鐠哄疇娴嗛崜宥嗗⒔鐞涘苯顦荤拋鎯у冀閸掓繂顫愰崠?*/
static void Boot_DeInitBeforeJump(void)
{
  uint32_t index;

  HAL_UART_DeInit(&huart3);
  HAL_RCC_DeInit();
  HAL_DeInit();

  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;

  __disable_irq();

  for (index = 0U; index < 8U; index++)
  {
    NVIC->ICER[index] = 0xFFFFFFFFU;
    NVIC->ICPR[index] = 0xFFFFFFFFU;
  }
}

/** 鐠哄疇娴嗛崚鐗堝瘹鐎规艾绨查悽銊ユ勾閸р偓閹笛嗩攽 */
static void Boot_JumpToApplication(uint32_t app_address)
{
  uint32_t stack_pointer = *((volatile uint32_t *)app_address);
  uint32_t reset_handler = *((volatile uint32_t *)(app_address + 4U));
  application_entry_t application_entry = (application_entry_t)reset_handler;

  Boot_DeInitBeforeJump();

  SCB->VTOR = app_address;
  __DSB();
  __ISB();

  __set_MSP(stack_pointer);
  __enable_irq();

  application_entry();
}

/** 鏉╂稑鍙咮oot鐢悂鈹楅幁銏狀槻濡€崇础 */
static void Boot_EnterRecoveryMode(OtaMetadata_TypeDef *metadata)
{
  uint32_t ota_result;

  Boot_Log("[BOOT] no bootable image, enter recovery ota mode\r\n");
  ota_result = OtaProtocol_RunSession(metadata, 1U);
  if (ota_result == OTA_RUN_RESULT_UPDATE_DONE)
  {
    Boot_Log("[BOOT] ota success, reboot now\r\n");
    HAL_Delay(20);
    NVIC_SystemReset();
  }

  while (1)
  {
    HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_2);
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_11);
    HAL_Delay(200);
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  OtaMetadata_TypeDef ota_metadata = {0};
  uint32_t ota_result;
  uint32_t target_slot;
  uint32_t target_address;
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
  Boot_Log("\r\n[BOOT] bootloader started\r\n");
  Boot_LogHexValue("[BOOT] metadata address: ", OTA_METADATA_ADDRESS);
  Boot_LogImageSummary(NULL);

  if (OtaMetadata_Load(&ota_metadata) == 0U)
  {
    Boot_Log("[BOOT] ota metadata invalid, rebuild default\r\n");
    Boot_InitMetadataDefaults(&ota_metadata);
    Boot_LogMetadataState(&ota_metadata);
    Boot_LogImageSummary(&ota_metadata);
  }
  else
  {
    Boot_Log("[BOOT] ota metadata load ok\r\n");
    Boot_LogMetadataState(&ota_metadata);
    Boot_LogImageSummary(&ota_metadata);
    Boot_SanitizeMetadata(&ota_metadata);
    Boot_Log("[BOOT] metadata after sanitize\r\n");
    Boot_LogMetadataState(&ota_metadata);
    Boot_LogImageSummary(&ota_metadata);
  }

  Boot_ApplyPendingSwitch(&ota_metadata);
  Boot_Log("[BOOT] metadata after apply switch\r\n");
  Boot_LogMetadataState(&ota_metadata);
  Boot_LogImageSummary(&ota_metadata);

  if ((ota_metadata.upgrade_flag == OTA_UPGRADE_FLAG_REQUEST) ||
      (ota_metadata.upgrade_flag == OTA_UPGRADE_FLAG_ACTIVE))
  {
    Boot_Log("[BOOT] force ota mode by upgrade flag\r\n");
    ota_result = OtaProtocol_RunSession(&ota_metadata, 1U);
  }
  else
  {
    ota_result = OtaProtocol_RunSession(&ota_metadata, 0U);
  }

  Boot_Log("[BOOT] metadata before select target\r\n");
  Boot_LogMetadataState(&ota_metadata);
  Boot_LogImageSummary(&ota_metadata);

  if (ota_result == OTA_RUN_RESULT_UPDATE_DONE)
  {
    Boot_Log("[BOOT] ota finished, reboot now\r\n");
    HAL_Delay(20);
    NVIC_SystemReset();
  }

  target_slot = Boot_SelectTargetSlot(&ota_metadata);
  if (target_slot == OTA_SLOT_NONE)
  {
    Boot_EnterRecoveryMode(&ota_metadata);
  }

  target_address = FlashIf_GetSlotAddress(target_slot);
  Boot_Log("[BOOT] selected target: ");
  Boot_Log(Boot_GetSlotName(target_slot));
  Boot_Log("\r\n");
  Boot_Log("[BOOT] target image valid, jumping...\r\n");
  HAL_Delay(20);
  Boot_JumpToApplication(target_address);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
void MPU_Config(void)
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
