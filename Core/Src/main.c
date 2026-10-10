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
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* ==============================================================================
 * UZAKTAN BOOTLOADER'A GİRİŞ MEKANİZMASI (Güç kesmeden update tetikleme)
 * ============================================================================== */
#define BOOT_REQUEST_MAGIC      0xB007B007UL
#define BOOT_REQUEST_ADDR       ((volatile uint32_t*)0x2001C000)  // RAM2 başlangıcı, uygulama ve bootloader ortak kullanır

#define CAN_ID_ENTER_BOOTLOADER 0x7F0   // Uzaktan bootloader tetikleme ID'si (Mevcut ID'lerle çakışmaz)

#define IDLE_TIMEOUT_NORMAL_MS   3000    // Fiziksel/soğuk boot: kısa bekleme (3 saniye)
#define IDLE_TIMEOUT_REMOTE_MS   30000   // Uzaktan tetiklenmiş: GUI ve Host yükleme yazılımına zaman tanı (30 saniye)

static const uint8_t BOOT_MAGIC_PAYLOAD[8] =
    { 0xDE, 0xAD, 0xC0, 0xDE, 0xB0, 0x07, 0x13, 0x37 }; // Güvenlik / Sabit tetikleme anahtarı

/* ==============================================================================
 * STM32F205 CAN BOOTLOADER TEST ADIMLARI KILAVUZU
 * ==============================================================================
 * Test etmek istediğiniz adımın '#define' satırını AÇIN, diğerlerini YORUMA ALIN.
 *
 * ------------------------------------------------------------------------------
 * ADIM 1 & 2: SADECE CAN LED BLINK (ST-LINK İLE İLK BOOTLOADER YÜKLEMESİ)
 * ------------------------------------------------------------------------------
 * Amaç: Kartın temel çalıştığını, kristal (HSE 12MHz -> PLL 120MHz) ve CAN LED'ini doğrulamak.
 * Hangi dosyada ne yapılacak?
 *   1) main.c:
 *      -> '#define STEP_TEST_1_2' satırını AÇIN.
 *      -> '//#define BOOTLOADER_BUILD' satırını AÇIN (Gerçek bootloader modudur).
 *   2) STM32F205RCTX_FLASH.ld:
 *      -> 'FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 32K' satırını AÇIN.
 *      -> '0x08008000' satırını YORUMA ALIN.
 *   3) system_stm32f2xx.c:
 *      -> 'VECT_TAB_OFFSET' değerinin '0x00000000U' olduğundan emin olun.
 * Beklenen Davranış:
 *   -> ST-Link ile yüklenir. Sadece LED_Can (PB10) yanıp söner (blink).
 *   -> LED_PWM (PB11) ve LED_Pin (PB9) tamamen sönük kalır.
 *   -> 3 sn timeout sonrası flash adresinde geçerli app yoksa beklemeye devam eder.
 *
 * ------------------------------------------------------------------------------
 * ADIM 3, 4, 5: CAN ÜZERİNDEN FIRMWARE ATMA & 1. LED TESTİ
 * ------------------------------------------------------------------------------
 * Amaç: CAN bootloader üzerinden ilk kullanıcı uygulamasının (.bin) yazılıp app'e zıplaması.
 * Hangi dosyada ne yapılacak?
 *   1) main.c:
 *      -> '#define STEP_TEST_3_5' satırını AÇIN.
 *      -> '//#define BOOTLOADER_BUILD' satırını MUTLAKA YORUMA ALIN! (Bu bir Application derlemesidir).
 *   2) STM32F205RCTX_FLASH.ld:
 *      -> 'FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 224K' satırını AÇIN.
 *      -> '0x08000000' satırını YORUMA ALIN.
 *   3) system_stm32f2xx.c:
 *      -> 'VECT_TAB_OFFSET' değerinin '0x00008000U' olduğundan emin olun.
 * Beklenen Davranış:
 *   -> Üretilen .bin dosyası CAN arayüzü ile karta yüklenir.
 *   -> Bootloader kodu yazar, CRC32 onaylar ve uygulamaya zıplar.
 *   -> LED_Can (PB10) ile birlikte 1. LED olan LED_PWM (PB11) blink yapar. LED_Pin (PB9) sönüktür.
 *
 * ------------------------------------------------------------------------------
 * ADIM 6: TEKRARLANABİLİRLİK TESTİ (CAN ÜZERİNDEN 2. LED TESTİ)
 * ------------------------------------------------------------------------------
 * Amaç: Yeni bir uygulamanın CAN üzerinden tekrar sorunsuz güncellenebildiğini doğrulamak.
 * Hangi dosyada ne yapılacak?
 *   1) main.c:
 *      -> '#define STEP_TEST_6_LED2' satırını AÇIN.
 *      -> '//#define BOOTLOADER_BUILD' satırını YORUMA ALIN.
 *   2) STM32F205RCTX_FLASH.ld:
 *      -> 'FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 224K' olmalıdır.
 *   3) system_stm32f2xx.c:
 *      -> 'VECT_TAB_OFFSET' değeri '0x00008000U' olmalıdır.
 * Beklenen Davranış:
 *   -> CAN üzerinden yeni .bin yüklendiğinde: LED_Can (PB10) ve 2. LED (LED_Pin / PB9) blink yapar.
 *   -> LED_PWM (PB11) sönük kalır.
 *
 * ------------------------------------------------------------------------------
 * ADIM 7: CAN VERİ AKIŞI, HIZ KONTROLÜ & UZAKTAN BOOTLOADER GEÇİŞİ
 * ------------------------------------------------------------------------------
 * Amaç: Uygulama çalışırken periyodik CAN telemetrisi basmak (0x720), CAN ile komut alarak
 *       LED hızını değiştirmek (0x710) ve uzaktan bootloader moduna geçişi (0x7F0) test etmek.
 * Hangi dosyada ne yapılacak?
 *   1) main.c:
 *      -> '#define STEP_TEST_7_CAN_FLOW' satırını AÇIN.
 *      -> '//#define BOOTLOADER_BUILD' satırını YORUMA ALIN.
 *   2) STM32F205RCTX_FLASH.ld:
 *      -> 'FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 224K' olmalıdır.
 * Beklenen Davranış:
 *   -> MCU her 500ms'de bir CAN hattına ID:0x720 telemetri paketi gönderir.
 *   -> Host'tan ID:0x710 ile veri gelirse blink periyodu anlık güncellenir.
 *   -> Host'tan ID:0x7F0 ile sihirli anahtar gönderilirse MCU kendini resetler ve
 *      30 saniye süreyle Bootloader modunda Host'tan yeni kod bekler!
 * ============================================================================== */

/* TEST SEÇİMİ (Yalnızca bir tanesini aktif bırakın): */
//#define STEP_TEST_1_2              	// Adım 1 ve 2: Sadece CAN LED blink, diğerleri sönük
//#define STEP_TEST_3_5           	// Adım 3, 4, 5: CAN LED + 1. LED (LED_PWM_Pin) blink
#define STEP_TEST_6_LED2     	// Adım 6: CAN LED + 2. LED (LED_Pin / PB9) blink
//#define STEP_TEST_7_CAN_FLOW    	// Adım 7: CAN telemetri ve periyot kontrol testi

/* DERLEME HEDEFİ:
 * - Bootloader (0x08000000 / Sector 0) derlenirken bu satırı AÇIN.
 * - Kullanıcı Uygulaması (0x08008000 / Sector 2+) derlenirken bu satırı YORUMA ALIN. */
//#define BOOTLOADER_BUILD

/* ==============================================================================
 * CAN BOOTLOADER TANIMLARI & PROTOKOLÜ (STM32F205_Can_Bootloader)
 * ============================================================================== */
#define APP_START_ADDRESS       0x08008000U
#define FLASH_END_ADDRESS       0x08040000U // 256 KB Flash (STM32F205RC)
#define APP_MAX_SIZE            (FLASH_END_ADDRESS - APP_START_ADDRESS) // 224 KB max

/* CAN ID Tanımları (Host <-> STM32F205_Can_Bootloader) */
#define CAN_ID_CMD_START        0x700 // Host -> Bootloader: Flash Erase & Başlat (DLC5: byte0=0x01, byte1-4=Firmware boyutu uint32 LE)
#define CAN_ID_DATA_PACKET      0x701 // Host -> Bootloader: Firmware Veri paketi (DLC 1..8)
#define CAN_ID_CMD_FINISH       0x702 // Host -> Bootloader: Transfer Bitti (DLC1: byte0=0x01)
#define CAN_ID_ACK_ERASE        0x703 // Bootloader -> Host: Erase Yanıtı (DLC1: 0x01=Başarılı, 0x00=Hata)
#define CAN_ID_ACK_PROGRESS     0x704 // Bootloader -> Host: İlerleme ACK (DLC4: Yazılan toplam byte uint32 LE)
#define CAN_ID_ACK_CRC          0x705 // Bootloader -> Host: Doğrulama CRC32 (DLC4: CRC32 uint32 LE)
#define CAN_ID_ERROR_REPORT     0x706 // Bootloader -> Host: Hata Paketi (DLC2: byte0=Hata Kodu, byte1=Ek Bilgi)
#define CAN_ID_IDLE_COUNTDOWN   0x707 // Bootloader -> Host: Boşta bekleme geri sayımı (DLC1: kalan saniye)

/* 0x706 Hata Kodları */
#define BL_ERR_ERASE            1 // Erase hatası (byte1 = HAL status veya sector)
#define BL_ERR_PROGRAM          2 // Flash Programlama hatası (byte1 = HAL status)
#define BL_ERR_SIZE_OVERFLOW    3 // Boyut aşımı (byte1 = 0x00)
#define BL_ERR_SYNC_SEQUENCE    4 // Sıra / Senkronizasyon hatası (byte1 = Beklenen durum vs.)

/* ACK İlerleme Gönderim Sıklığı */
#define ACK_PROGRESS_PACKET_INTERVAL  32 // Her 32 veri paketinde bir 0x704 ACK gönder

/* Ring Buffer Yapılandırması */
#define RX_QUEUE_SIZE           64 // 64 adet CAN veri paketi kuyruğu (64 * 8 = 512 byte buffer)

/* Bootloader Durum Makinesi (State Machine) */
typedef enum {
  BL_STATE_IDLE = 0,      // Başlangıç, 3 sn komut bekleme durumu
  BL_STATE_ERASING,       // Flash erase işlemi yürütülüyor
  BL_STATE_RECEIVING,     // Veri paketleri kuyruktan okunup flash'a yazılıyor
  BL_STATE_VERIFYING,     // CRC32 hesaplanıp host'a iletiliyor
  BL_STATE_DONE,          // Tamamlandı, uygulamaya geçiş yapılıyor
  BL_STATE_ERROR          // Hata oluştu, sistem durduruldu
} BootloaderState_t;

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan1;

/* Tek Zamanlayıcı (Single Timer): TIM3 (1 us tick, 1 ms periyot) */
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */

CAN_RxHeaderTypeDef RxHeader;
uint8_t RxData[8];

/* Bootloader Ring Buffer / Queue Tanımı (ISR -> while döngüsü için) */
typedef struct {
  uint8_t data[8];
  uint8_t length;
} CanRxPacket_t;

static volatile CanRxPacket_t rxQueue[RX_QUEUE_SIZE];
static volatile uint16_t rxQueueHead = 0;
static volatile uint16_t rxQueueTail = 0;
static volatile uint8_t rxQueueOverflow = 0;

/* Bootloader Durum ve Kontrol Değişkenleri */
static volatile BootloaderState_t blState = BL_STATE_IDLE;
static uint8_t remoteUpdateRequested = 0;
static uint32_t firmwareExpectedSize = 0;
static uint32_t totalBytesWritten = 0;
static uint32_t flashWritePtr = APP_START_ADDRESS;
static uint32_t packetSequenceCount = 0;
static uint8_t lastErrorCode = 0;
static uint8_t lastErrorDetail = 0;

/* Adım 7 ve LED kontrol değişkenleri */
uint32_t ledBlinkPeriodMs = 150;    // Varsayılan LED yanıp sönme periyodu (ms)
uint32_t lastBlinkTick = 0;
uint32_t lastCanTxTick = 0;
uint32_t canTxCounter = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN1_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */

void EraseAppRegion(void);
void JumpToApplication(void);
void Bootloader_MainLoop(uint32_t bootStartTick);
HAL_StatusTypeDef Bootloader_EraseForSize(uint32_t firmwareSize);
uint32_t Bootloader_CalculateCRC32(uint32_t startAddr, uint32_t length);
void Bootloader_SendAckErase(uint8_t status);
void Bootloader_SendProgress(uint32_t bytesWritten);
void Bootloader_SendCRC(uint32_t crcValue);
void Bootloader_SendError(uint8_t errCode, uint8_t errDetail);

void Step_LED_Init(void);
void Step_LED_Update(void);
void Step7_CAN_SendTelemetry(void);

//Can Rx dinleme için ama burada sadece bootloader olarak var
static void CAN_Bootloader_DispatchMessage(CAN_RxHeaderTypeDef *header, uint8_t *data);

//Bootloader check fonksiyonu
static uint8_t Bootloader_CheckRemoteUpdateRequest(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

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
  MX_CAN1_Init();
  MX_TIM3_Init();
  MX_FATFS_Init();

  /* USER CODE BEGIN 2 */
  // LED setupları
  Step_LED_Init();

  // Güç kapatmadan bootloader initleri (RAM2 kontrolü)
  remoteUpdateRequested = Bootloader_CheckRemoteUpdateRequest();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
#if defined(BOOTLOADER_BUILD)
  uint32_t bootStart = HAL_GetTick();
#endif

  /* STM32F205_Can_Bootloader: Tek Timer (TIM3) zaman tabanı sayıcısını başlat (1 us tick, 1 ms periyot) */
  HAL_TIM_Base_Start(&htim3);

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if defined(STM32F205_CAN_BOOTLOADER_CONFIG) && (STM32F205_CAN_BOOTLOADER_CONFIG == 1)
    /* Test adımına göre LED durumlarını periyodik olarak toggle et */
    Step_LED_Update();

    #if defined(STEP_TEST_7_CAN_FLOW)
    /* Adım 7: 500 ms aralıklarla CAN telemetri paketi gönder (ID: 0x720) */
    if (HAL_GetTick() - lastCanTxTick >= 500)
    {
      lastCanTxTick = HAL_GetTick();
      Step7_CAN_SendTelemetry();
    }
    #endif

    #if defined(BOOTLOADER_BUILD) && !defined(STEP_TEST_7_CAN_FLOW)
    /* Sadece gerçek Bootloader (0x08000000 / Sector 0) derlemesinde çalışır:
     * - Timeout süresince CAN komutlarını bekler ve firmware yazar.
     * - Süre dolunca veya komut tamamlanınca 0x08008000 adresindeki uygulamaya zıplar. */
    Bootloader_MainLoop(bootStart);
    #endif
#endif
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 12;
  RCC_OscInitStruct.PLL.PLLN = 240;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 6;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_2TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  CAN_FilterTypeDef sFilterConfig;
  sFilterConfig.FilterBank = 0;
  sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterIdHigh = 0; sFilterConfig.FilterIdLow = 0;
  sFilterConfig.FilterMaskIdHigh = 0; sFilterConfig.FilterMaskIdLow = 0;
  sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
  sFilterConfig.FilterActivation = ENABLE;
  sFilterConfig.SlaveStartFilterBank = 14;
  HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig);
  HAL_CAN_Start(&hcan1);
  HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  /* STM32F205_Can_Bootloader Tek Zamanlayıcı (TIM3) Yapılandırması:
   * - APB1 Timer Frekansı = 60 MHz (HCLK 120MHz / APB1 Prescaler 4 -> 30MHz x 2 = 60MHz).
   * - Prescaler = 60 - 1 = 59: Sayıcı Frekansı = 1.000.000 Hz = 1 MHz (Her 1 tick = 1 mikrosaniye).
   * - Period (AutoReload) = 1000 - 1 = 999: Sayıcı 1000 mikrosaniyede bir taşar (Tam 1 milisaniye / 1 kHz).
   * - Bu sayede timer kayması yaşanmaz; mikrosaniye sayımı ve 1 ms zaman tabanı kararlı çalışır. */
  htim3.Init.Prescaler = 60 - 1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1000 - 1;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED_Can_Pin|LED_PWM_Pin|LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LED_Can_Pin LED_PWM_Pin LED_Pin */
  GPIO_InitStruct.Pin = LED_Can_Pin|LED_PWM_Pin|LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief  CAN FIFO0 Alım Kesmesi (ISR)
  * @note   Bu fonksiyon içinde flash yazma/silme gibi bloklayıcı işlemler YAPILMAZ!
  *         Gelen veri ring buffer'a kopyalanır ve ana döngüye devredilir.
  */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData) != HAL_OK)
    return;

  CAN_Bootloader_DispatchMessage(&RxHeader, RxData);
}
/**
  * @brief  MCU -> Host: 0x703 Erase ACK paketi gönderir.
  */
void Bootloader_SendAckErase(uint8_t status)
{
  CAN_TxHeaderTypeDef txHeader;
  uint8_t txData[1];
  uint32_t txMailbox;

  txHeader.StdId = CAN_ID_ACK_ERASE;
  txHeader.IDE = CAN_ID_STD;
  txHeader.RTR = CAN_RTR_DATA;
  txHeader.DLC = 1;
  txHeader.TransmitGlobalTime = DISABLE;
  txData[0] = status;

  HAL_CAN_AddTxMessage(&hcan1, &txHeader, txData, &txMailbox);
}

/**
  * @brief  CAN'dan gelen mesajı StdId'sine göre ilgili işleyiciye yönlendirir.
  * @note   ISR (kesme) bağlamından çağrılır — burada flash yazma/silme gibi
  *         BLOKLAYICI işlemler YAPILMAZ. Sadece durum değişimi / kuyruğa ekleme yapılır.
  */
static void CAN_Bootloader_DispatchMessage(CAN_RxHeaderTypeDef *header, uint8_t *data){
	/* --- Uzaktan bootloader'a giriş isteği: build tipinden bağımsız, her zaman dinlenir --- */
	if (header->StdId == CAN_ID_ENTER_BOOTLOADER && header->DLC == 8 && memcmp(data, BOOT_MAGIC_PAYLOAD, 8) == 0){
		*BOOT_REQUEST_ADDR = BOOT_REQUEST_MAGIC;
		NVIC_SystemReset();
		return; /* Buraya asla gelinmez */
	}

	#if defined(STEP_TEST_7_CAN_FLOW)
	/* --- Adım 7: CAN üzerinden LED periyodu ayarlama (StdId: 0x710) --- */
	if (header->StdId == 0x710 && header->DLC >= 2){
		uint16_t newPeriod = (uint16_t)(data[0] | (data[1] << 8));
		if (newPeriod >= 50 && newPeriod <= 5000){
			ledBlinkPeriodMs = newPeriod;
		}
	}

	#elif defined(BOOTLOADER_BUILD)
	/* --- Bootloader Paket İşleme --- */
	switch (header->StdId){
	case CAN_ID_CMD_START:
		/* 0x700: Erase ve Güncelleme Başlat */
		if (blState == BL_STATE_IDLE || blState == BL_STATE_ERROR){
			if (header->DLC >= 5 && data[0] == 0x01){
				firmwareExpectedSize = (uint32_t)(data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24));
				blState = BL_STATE_ERASING;
			}
			else if (header->DLC == 1 && data[0] == 0x01){
				firmwareExpectedSize = APP_MAX_SIZE;
				blState = BL_STATE_ERASING;
			}
			else{
				lastErrorCode = BL_ERR_SYNC_SEQUENCE;
				lastErrorDetail = 0x01;
				blState = BL_STATE_ERROR;
			}
		}
		break;
	case CAN_ID_DATA_PACKET:
		/* 0x701: Firmware Veri Paketi (DLC 1..8) */
		if (blState == BL_STATE_RECEIVING){
			uint16_t nextHead = (rxQueueHead + 1) % RX_QUEUE_SIZE;
			if (nextHead != rxQueueTail){
				for (uint8_t i = 0; i < header->DLC; i++){
					rxQueue[rxQueueHead].data[i] = data[i];
				}
				rxQueue[rxQueueHead].length = header->DLC;
				rxQueueHead = nextHead;
			}
			else{
				rxQueueOverflow = 1;
				lastErrorCode = BL_ERR_PROGRAM;
				lastErrorDetail = 0xFF;
				blState = BL_STATE_ERROR;
			}
		}
		break;
	case CAN_ID_CMD_FINISH:
		/* 0x702: Güncelleme Tamamlandı */
		if (blState == BL_STATE_RECEIVING){
			blState = BL_STATE_VERIFYING;
		}
		break;
	default:
		break;
	}
	#endif
}

/**
  * @brief  MCU -> Host: Bootloader'ın timeout'a kadar kalan süresini (saniye) bildirir.
  */
void Bootloader_SendIdleCountdown(uint8_t secondsRemaining){
	CAN_TxHeaderTypeDef txHeader;
	uint8_t txData[1];
	uint32_t txMailbox;

	txHeader.StdId = CAN_ID_IDLE_COUNTDOWN;
	txHeader.IDE = CAN_ID_STD;
	txHeader.RTR = CAN_RTR_DATA;
	txHeader.DLC = 1;
	txHeader.TransmitGlobalTime = DISABLE;
	txData[0] = secondsRemaining;

	HAL_CAN_AddTxMessage(&hcan1, &txHeader, txData, &txMailbox);
}

/**
  * @brief  RAM2'deki (0x2001C000) magic bayrağını kontrol eder.
  *         App tarafından NVIC_SystemReset() öncesi bırakılmış bir güncelleme
  *         isteği varsa true döner ve bayrağı temizler (tek seferlik kullanım).
  */
static uint8_t Bootloader_CheckRemoteUpdateRequest(void){
	if (*BOOT_REQUEST_ADDR == BOOT_REQUEST_MAGIC){
		*BOOT_REQUEST_ADDR = 0;
		return 1;
	}
	return 0;
}

/**
  * @brief  MCU -> Host: 0x704 İlerleme ACK paketi gönderir (toplam yazılan byte sayısı).
  */
void Bootloader_SendProgress(uint32_t bytesWritten)
{
  CAN_TxHeaderTypeDef txHeader;
  uint8_t txData[4];
  uint32_t txMailbox;

  txHeader.StdId = CAN_ID_ACK_PROGRESS;
  txHeader.IDE = CAN_ID_STD;
  txHeader.RTR = CAN_RTR_DATA;
  txHeader.DLC = 4;
  txHeader.TransmitGlobalTime = DISABLE;

  txData[0] = (uint8_t)(bytesWritten & 0xFF);
  txData[1] = (uint8_t)((bytesWritten >> 8) & 0xFF);
  txData[2] = (uint8_t)((bytesWritten >> 16) & 0xFF);
  txData[3] = (uint8_t)((bytesWritten >> 24) & 0xFF);

  HAL_CAN_AddTxMessage(&hcan1, &txHeader, txData, &txMailbox);
}

/**
  * @brief  MCU -> Host: 0x705 Doğrulama CRC32 paketi gönderir.
  */
void Bootloader_SendCRC(uint32_t crcValue)
{
  CAN_TxHeaderTypeDef txHeader;
  uint8_t txData[4];
  uint32_t txMailbox;

  txHeader.StdId = CAN_ID_ACK_CRC;
  txHeader.IDE = CAN_ID_STD;
  txHeader.RTR = CAN_RTR_DATA;
  txHeader.DLC = 4;
  txHeader.TransmitGlobalTime = DISABLE;

  txData[0] = (uint8_t)(crcValue & 0xFF);
  txData[1] = (uint8_t)((crcValue >> 8) & 0xFF);
  txData[2] = (uint8_t)((crcValue >> 16) & 0xFF);
  txData[3] = (uint8_t)((crcValue >> 24) & 0xFF);

  HAL_CAN_AddTxMessage(&hcan1, &txHeader, txData, &txMailbox);
}

/**
  * @brief  MCU -> Host: 0x706 Genel hata paketi gönderir.
  */
void Bootloader_SendError(uint8_t errCode, uint8_t errDetail)
{
  CAN_TxHeaderTypeDef txHeader;
  uint8_t txData[2];
  uint32_t txMailbox;

  txHeader.StdId = CAN_ID_ERROR_REPORT;
  txHeader.IDE = CAN_ID_STD;
  txHeader.RTR = CAN_RTR_DATA;
  txHeader.DLC = 2;
  txHeader.TransmitGlobalTime = DISABLE;

  txData[0] = errCode;
  txData[1] = errDetail;

  HAL_CAN_AddTxMessage(&hcan1, &txHeader, txData, &txMailbox);
}

/**
  * @brief  Gelen dosya boyutuna göre gereken sektörleri hesaplayıp siler.
  * @note   Sector 2 (16KB): 0x08008000 - 0x0800BFFF
  *         Sector 3 (16KB): 0x0800C000 - 0x0800FFFF
  *         Sector 4 (64KB): 0x08010000 - 0x0801FFFF
  *         Sector 5 (128KB): 0x08020000 - 0x0803FFFF
  */
HAL_StatusTypeDef Bootloader_EraseForSize(uint32_t firmwareSize)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t sectorError;
  uint32_t nbSectors = 1;

  if (firmwareSize <= 16 * 1024)
  {
    nbSectors = 1; // Sadece Sector 2 (16 KB)
  }
  else if (firmwareSize <= 32 * 1024)
  {
    nbSectors = 2; // Sector 2, 3 (32 KB)
  }
  else if (firmwareSize <= 96 * 1024)
  {
    nbSectors = 3; // Sector 2, 3, 4 (96 KB)
  }
  else
  {
    nbSectors = 4; // Sector 2, 3, 4, 5 (224 KB - tüm app alanı)
  }

  HAL_StatusTypeDef status = HAL_FLASH_Unlock();
  if (status != HAL_OK) return status;

  eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
  eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  eraseInit.Sector       = FLASH_SECTOR_2;
  eraseInit.NbSectors    = nbSectors;

  status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);
  HAL_FLASH_Lock();

  return status;
}

/**
  * @brief  Eski erase fonksiyonu ile geriye dönük uyumluluk
  */
void EraseAppRegion(void)
{
  Bootloader_EraseForSize(APP_MAX_SIZE);
}

/**
  * @brief  Yazılan flash bölgesi için yazılımsal standart CRC32 hesaplar.
  */
uint32_t Bootloader_CalculateCRC32(uint32_t startAddr, uint32_t length)
{
  uint32_t crc = 0xFFFFFFFF;
  const uint8_t *pData = (const uint8_t *)startAddr;

  for (uint32_t i = 0; i < length; i++)
  {
    crc ^= pData[i];
    for (uint8_t j = 0; j < 8; j++)
    {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xEDB88320;
      else
        crc = (crc >> 1);
    }
  }
  return ~crc;
}

/**
  * @brief  Ana döngüde çalışan Bootloader Durum Makinesi (State Machine)
  * @note   Tüm FLASH yazma/silme ve ACK işlemleri burada, ISR dışında icra edilir.
  */
void Bootloader_MainLoop(uint32_t bootStartTick){
	switch (blState){
		case BL_STATE_IDLE:{
			uint32_t idleTimeout = remoteUpdateRequested ? IDLE_TIMEOUT_REMOTE_MS : IDLE_TIMEOUT_NORMAL_MS;
			uint32_t elapsed = HAL_GetTick() - bootStartTick;
			/* Her 1000ms'de bir kalan süreyi CAN'dan bildir */
			static uint32_t lastCountdownTick = 0;
			if (HAL_GetTick() - lastCountdownTick >= 1000){
				lastCountdownTick = HAL_GetTick();
				uint32_t remainingMs = (elapsed < idleTimeout) ? (idleTimeout - elapsed) : 0;
				Bootloader_SendIdleCountdown((uint8_t)(remainingMs / 1000));
			}

			if (elapsed > idleTimeout){
				JumpToApplication();
			}
			break;
		}
		case BL_STATE_ERASING:
			// Boyut kontrolü
			if (firmwareExpectedSize == 0 || firmwareExpectedSize > APP_MAX_SIZE){
				lastErrorCode = BL_ERR_SIZE_OVERFLOW;
				lastErrorDetail = 0x01;
				blState = BL_STATE_ERROR;
				Bootloader_SendError(lastErrorCode, lastErrorDetail);
				break;
			}

			// Flash sektörlerini sil
			if (Bootloader_EraseForSize(firmwareExpectedSize) == HAL_OK){
				flashWritePtr = APP_START_ADDRESS;
				totalBytesWritten = 0;
				packetSequenceCount = 0;
				rxQueueHead = 0;
				rxQueueTail = 0;
				rxQueueOverflow = 0;

				blState = BL_STATE_RECEIVING;
				Bootloader_SendAckErase(0x01); // Başarılı ACK
				}
			else{
				lastErrorCode = BL_ERR_ERASE;
				lastErrorDetail = 0x02;
				blState = BL_STATE_ERROR;
				Bootloader_SendAckErase(0x00); // Erase Başarısız
				Bootloader_SendError(lastErrorCode, lastErrorDetail);
			}
			break;
		case BL_STATE_RECEIVING:
		case BL_STATE_VERIFYING:
			// Kuyruktaki (Ring Buffer) paketleri Flash'a yaz
			while (rxQueueTail != rxQueueHead){
				CanRxPacket_t packet;
				packet = rxQueue[rxQueueTail];
				rxQueueTail = (rxQueueTail + 1) % RX_QUEUE_SIZE;

				// Boyut aşımı kontrolü
				if ((totalBytesWritten + packet.length) > firmwareExpectedSize ||
						(flashWritePtr + packet.length) > FLASH_END_ADDRESS){

					lastErrorCode = BL_ERR_SIZE_OVERFLOW;
					lastErrorDetail = 0x02;
					blState = BL_STATE_ERROR;
					Bootloader_SendError(lastErrorCode, lastErrorDetail);
					return;
				}

				// Flash programlama (ISR dışında, güvenli)
				HAL_FLASH_Unlock();
				for (uint8_t i = 0; i < packet.length; i++){
					if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, flashWritePtr, packet.data[i]) != HAL_OK){
						HAL_FLASH_Lock();
						lastErrorCode = BL_ERR_PROGRAM;
						lastErrorDetail = 0x01;
						blState = BL_STATE_ERROR;
						Bootloader_SendError(lastErrorCode, lastErrorDetail);
						return;
					}
					flashWritePtr++;
					totalBytesWritten++;
				}
				HAL_FLASH_Lock();
				packetSequenceCount++;
				// Her N pakette bir ACK ilerleme bildirimi gönder (0x704)
				if ((packetSequenceCount % ACK_PROGRESS_PACKET_INTERVAL) == 0){
					Bootloader_SendProgress(totalBytesWritten);
				}
			}
			// Eğer 0x702 (FINISH) komutu gelmişse ve tüm kuyruk flash'a yazılmışsa doğrulamaya geç
			if (blState == BL_STATE_VERIFYING && rxQueueTail == rxQueueHead){
				// Son yazılan byte adedini kesin olarak host'a ACK et
				Bootloader_SendProgress(totalBytesWritten);
				// CRC32 hesapla ve ilet
				uint32_t appCRC = Bootloader_CalculateCRC32(APP_START_ADDRESS, totalBytesWritten);
				Bootloader_SendCRC(appCRC);
				HAL_Delay(100); // CAN mesajının hattan çıkması için kısa bekleme
				blState = BL_STATE_DONE;
			}
			break;
		case BL_STATE_DONE:
			JumpToApplication();
			break;

		case BL_STATE_ERROR:
			// Hata durumunda kalınır, isteğe bağlı olarak LED hızlı flaş yapabilir
			HAL_Delay(10);
			break;
		default:
			break;
	}
}

void JumpToApplication(void)
{
  uint32_t appStack = *(__IO uint32_t*)APP_START_ADDRESS;
  uint32_t appReset = *(__IO uint32_t*)(APP_START_ADDRESS + 4);

  // Stack pointer geçerli bir RAM adresinde mi? (0x20000000 - 0x20020000)
  if ((appStack & 0x2FFE0000) != 0x20000000)
  {
    // =========================================================================
    // TEŞHİS 1: Atlama Şartı Sağlanamadı! (Flash adresinde geçerli kod yok)
    // CAN üzerinden 0x706 Hata Paketi gönder (Hata: 4, Detay: appStack'in ilk byte'ı)
    // Ve LED_Pin (PB9) pinini SABİT YAK (gözünüzle hatayı görün)
    // =========================================================================
    Bootloader_SendError(BL_ERR_SYNC_SEQUENCE, (uint8_t)(appStack >> 24));
    HAL_GPIO_WritePin(GPIOB, LED_Pin, GPIO_PIN_SET); // PB9 SABİT YANSIN
    return; // geçerli bir app yok, atlama
  }

  // =========================================================================
  // TEŞHİS 2: appStack GEÇERLİ! Atlama başlıyor!
  // Atlarken LED_PWM_Pin'i (PB11) anlık yakıp söndürerek atladiğini teyit et
  // =========================================================================
  HAL_GPIO_WritePin(GPIOB, LED_PWM_Pin, GPIO_PIN_SET);
  HAL_Delay(50);
  HAL_GPIO_WritePin(GPIOB, LED_PWM_Pin, GPIO_PIN_RESET);

  // 1. CAN bildirimlerini ve donanımını kapat
  HAL_CAN_DeactivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);
  HAL_CAN_Stop(&hcan1);

  // 2. SysTick zamanlayıcısını tamamen kapat ve temizle
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  // 3. Tüm kesmeleri devre dışı bırak ve bekleyen kesme bayraklarını temizle
  __disable_irq();
  for (uint8_t i = 0; i < 8; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }

  // 4. Çevre birimleri sıfırla (Saatler 120MHz'de açık kalır, böylece app HSE kilitlenmesi yaşamaz)
  HAL_DeInit();

  // 5. İşlemci modunu varsayılana getir (Privileged Thread Mode, Main Stack Pointer MSP)
  __set_CONTROL(0);

  // 6. Vektör tablosunu yeni uygulamanın adresine (0x08008000) taşı
  SCB->VTOR = APP_START_ADDRESS;

  // 7. Hafıza bariyerleri: Tüm bellek işlemlerinin tamamlandığından emin ol
  __DSB();
  __ISB();

  // 8. Yeni uygulamanın Stack Pointer'ını yükle
  __set_MSP(appStack);

  // 9. Yeni uygulamanın Reset Handler'ına zıpla (Branch to Reset_Handler)
  __asm volatile (
    "cpsie i \n"     /* Kesmeleri CPU seviyesinde aç */
    "bx %0   \n"     /* Reset_Handler'a dallan */
    : : "r" (appReset) : "memory"
  );

  // Buraya asla gelinmemeli
  while (1) {}
}

/**
  * @brief Adım ayarlarına göre LED'lerin başlangıç durumunu ayarlar.
  */
void Step_LED_Init(void)
{
  // Tüm test LED'lerini önce kapalı (RESET) konumuna getir (EEPROM_WC pini hariç)
  HAL_GPIO_WritePin(GPIOB, LED_Can_Pin | LED_PWM_Pin | LED_Pin, GPIO_PIN_RESET);
}

/**
  * @brief Test adımına göre ilgili LED'leri periyodik olarak toggle eder.
  */
void Step_LED_Update(void)
{
  if (HAL_GetTick() - lastBlinkTick >= ledBlinkPeriodMs)
  {
    lastBlinkTick = HAL_GetTick();

    // CAN LED'i her adımda blink yapar
    HAL_GPIO_TogglePin(GPIOB, LED_Can_Pin);

    #if defined(STEP_TEST_1_2)
    // Adım 1 & 2: Diğer LED'ler kesinlikle sönük kalır
    HAL_GPIO_WritePin(GPIOB, LED_PWM_Pin | LED_Pin, GPIO_PIN_RESET);

    #elif defined(STEP_TEST_3_5)
    // Adım 3, 4, 5: CAN LED + 1. LED (LED_PWM_Pin) birlikte blink yapar
    HAL_GPIO_TogglePin(GPIOB, LED_PWM_Pin);
    HAL_GPIO_WritePin(GPIOB, LED_Pin, GPIO_PIN_RESET);

    #elif defined(STEP_TEST_6_LED2)
    // Adım 6-A: CAN LED + 2. LED (LED_Pin / PB9) blink yapar
    HAL_GPIO_TogglePin(GPIOB, LED_Pin);
    HAL_GPIO_WritePin(GPIOB, LED_PWM_Pin, GPIO_PIN_RESET);

    #elif defined(STEP_TEST_7_CAN_FLOW)
    // Adım 7: CAN LED + LED_Pin aktif blink, CAN üzerinden veri akışı devam eder
    HAL_GPIO_TogglePin(GPIOB, LED_Pin);
    #endif
  }
}

/**
  * @brief Adım 7 için CAN üzerinden telemetri/durum paketi gönderir (StdId: 0x720).
  */
void Step7_CAN_SendTelemetry(void)
{
  CAN_TxHeaderTypeDef txHeader;
  uint8_t txData[8];
  uint32_t txMailbox;

  txHeader.StdId = 0x720;
  txHeader.IDE = CAN_ID_STD;
  txHeader.RTR = CAN_RTR_DATA;
  txHeader.DLC = 8;
  txHeader.TransmitGlobalTime = DISABLE;

  // Gönderilen telemetri yükü:
  // Byte 0-3: Gönderim sayacı (uint32)
  // Byte 4-5: Mevcut LED blink periyodu (uint16 ms)
  // Byte 6: CAN durumu (0xAA: Canlı/OK)
  // Byte 7: Sabit durum kontrol baytı (0x55)
  canTxCounter++;
  txData[0] = (uint8_t)(canTxCounter & 0xFF);
  txData[1] = (uint8_t)((canTxCounter >> 8) & 0xFF);
  txData[2] = (uint8_t)((canTxCounter >> 16) & 0xFF);
  txData[3] = (uint8_t)((canTxCounter >> 24) & 0xFF);
  txData[4] = (uint8_t)(ledBlinkPeriodMs & 0xFF);
  txData[5] = (uint8_t)((ledBlinkPeriodMs >> 8) & 0xFF);
  txData[6] = 0xAA;
  txData[7] = 0x55;

  HAL_CAN_AddTxMessage(&hcan1, &txHeader, txData, &txMailbox);
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
