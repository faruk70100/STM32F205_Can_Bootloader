# STM32F205 CAN Bootloader & Test Uygulaması

STM32F205 serisi mikrodenetleyiciler için optimize edilmiş; güvenilir, hızlı, yazılımsal CRC32 doğrulamalı ve cihazın enerjisini kesmeden uzaktan tetiklenebilen profesyonel bir CAN Bootloader ve firmware test uygulamasıdır.

---

## 📌 Donanım Özellikleri

- **MCU:** STM32F205RCT6 (ARM Cortex-M3, 120 MHz SYSCLK)
- **Hafıza:** 256 KB Flash, 128 KB RAM (80 KB SRAM1 + 16 KB SRAM2)
- **HSE Kristal:** 12 MHz Harici Kristal (PLL ile 120 MHz sistem saati)
- **CAN Hattı (CAN1):** 1.000.000 bps (1 Mbit/s)
  - `PA11`: CAN1_RX
  - `PA12`: CAN1_TX
- **Durum LED'leri:**
  - `PB10`: `LED_Can` (CAN Canlılık & İletişim LED'i - Her adımda blink yapar)
  - `PB11`: `LED_PWM` (1. Durum / Test LED'i)
  - `PB9`: `LED_Pin` (2. Durum / Tekrarlanabilirlik Test LED'i)
- **Tek Zamanlayıcı (TIM3):**
  - APB1 Timer Saati: 60 MHz
  - Prescaler: `59` (1 MHz sayıcı, her tick 1 µs)
  - Period: `999` (1 ms periyot / 1 kHz)

---

## 🗺️ Flash ve RAM Bellek Haritası

| Bölge | Sektörler | Flash Adres Aralığı | Boyut | Kullanım Amacı |
| :--- | :--- | :--- | :--- | :--- |
| **Bootloader** | Sector 0 - 1 | `0x08000000` - `0x08007FFF` | 32 KB | CAN Bootloader Kodu |
| **Uygulama** | Sector 2 | `0x08008000` - `0x0800BFFF` | 16 KB | Kullanıcı Firmware |
| | Sector 3 | `0x0800C000` - `0x0800FFFF` | 16 KB | Kullanıcı Firmware |
| | Sector 4 | `0x08010000` - `0x0801FFFF` | 64 KB | Kullanıcı Firmware |
| | Sector 5 | `0x08020000` - `0x0803FFFF` | 128 KB | Kullanıcı Firmware |
| **Toplam Uygulama** | **Sector 2..5** | **`0x08008000` - `0x0803FFFF`** | **224 KB** | Kullanıcı Uygulama Alanı |

> **RAM2 (0x2001C000) Paylaşımı:** Güç kesmeden uzaktan bootloader tetiklemek için `0x2001C000` adresine `0xB007B007` (Magic Word) yazılır ve `NVIC_SystemReset()` çağrılır. Bootloader açılışta bu adresi kontrol ederek 30 saniye süreyle Host'u bekler.

---

## 📡 CAN Haberleşme Protokolü

Standart 11-bit CAN ID'leri kullanılmaktadır:

| CAN ID | Yön | DLC | Açıklama | Veri Yükü (Payload) |
| :---: | :---: | :---: | :--- | :--- |
| `0x700` | Host $\rightarrow$ MCU | 5 | Flash Erase & Başlat | `byte0=0x01`, `byte1..4=Firmware boyutu (uint32 LE)` |
| `0x701` | Host $\rightarrow$ MCU | 1..8 | Veri Paketi | Flash'a yazılacak ikili veri |
| `0x702` | Host $\rightarrow$ MCU | 1 | Transfer Bitti | `byte0=0x01` |
| `0x703` | MCU $\rightarrow$ Host | 1 | Erase Yanıtı | `0x01`: Başarılı, `0x00`: Hata |
| `0x704` | MCU $\rightarrow$ Host | 4 | İlerleme ACK (Her 32 pakette bir) | Toplam yazılan byte miktarı (uint32 LE) |
| `0x705` | MCU $\rightarrow$ Host | 4 | CRC32 Doğrulama Paketi | Yazılan firmware'in CRC32 değeri (uint32 LE) |
| `0x706` | MCU $\rightarrow$ Host | 2 | Hata Raporu | `byte0=Hata Kodu`, `byte1=Ek Bilgi` |
| `0x707` | MCU $\rightarrow$ Host | 1 | Boşta Geri Sayım | Kalan saniye (Örn: 3, 2, 1) |
| `0x710` | Host $\rightarrow$ MCU | 2 | Uygulama: LED Hız Ayarı | Yeni LED periyodu ms (uint16 LE, 50 - 5000 ms) |
| `0x720` | MCU $\rightarrow$ Host | 8 | Uygulama: Canlılık Telemetrisi | 500 ms periyotla sayaç, LED periyodu ve durum |
| `0x7F0` | Host $\rightarrow$ MCU | 8 | Uzaktan Bootloader Tetikleme | `0xDE 0xAD 0xC0 0xDE 0xB0 0x07 0x13 0x37` |

---

## 🧪 Adım Adım Test Kılavuzu

### ADIM 1 & 2: Temel Bootloader Doğrulama (ST-Link ile)
- **Amaç:** MCU, 120 MHz kristal ve CAN LED'inin sağlamlığını doğrulamak.
- **`Core/Src/main.c`:**
  ```c
  #define STEP_TEST_1_2
  #define BOOTLOADER_BUILD
  ```
- **`STM32F205RCTX_FLASH.ld`:**
  ```ld
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 32K
  ```
- **Davranış:** Sadece `LED_Can` (PB10) yanıp söner. PB11 ve PB9 sönük kalır.

---

### ADIM 3, 4, 5: CAN Üzerinden Firmware Yükleme & 1. LED Testi
- **Amaç:** CAN üzerinden ilk uygulamanın (.bin) flash'a yazılıp uygulamaya atlandığını teyit etmek.
- **`Core/Src/main.c`:**
  ```c
  #define STEP_TEST_3_5
  //#define BOOTLOADER_BUILD  // MUTLAKA YORUMDA OLMALI
  ```
- **`STM32F205RCTX_FLASH.ld`:**
  ```ld
  FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 224K
  ```
- **Davranış:** CAN ile firmware atıldığında `LED_Can` (PB10) ve `LED_PWM` (PB11) birlikte senkron yanıp söner.

---

### ADIM 6: Tekrarlanabilirlik Testi (2. LED Testi)
- **Amaç:** Tekrar CAN üzerinden güncelleme yapılabildiğini doğrulamak.
- **`Core/Src/main.c`:**
  ```c
  #define STEP_TEST_6_LED2
  //#define BOOTLOADER_BUILD
  ```
- **`STM32F205RCTX_FLASH.ld`:**
  ```ld
  FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 224K
  ```
- **Davranış:** CAN'dan yeni .bin atıldığında `LED_Can` (PB10) ve `LED_Pin` (PB9) yanıp söner, PB11 söner.

---

### ADIM 7: CAN Telemetri & Uzaktan Bootloader Geçişi
- **Amaç:** Periyodik telemetri göndermek, CAN'dan hız ayarlamak ve gücü kesmeden bootloader'a geçmek.
- **`Core/Src/main.c`:**
  ```c
  #define STEP_TEST_7_CAN_FLOW
  //#define BOOTLOADER_BUILD
  ```
- **Davranış:**
  - MCU her 500 ms'de bir `0x720` telemetrisi gönderir.
  - `0x710` ile hız değiştirilebilir.
  - `0x7F0` ile sihirli anahtar gönderildiğinde sistem resetlenip 30 saniye bootloader modunda bekler.

---

## 🛠️ Derleme

### STM32CubeIDE ile:
`File` $\rightarrow$ `Import` $\rightarrow$ `Existing Projects into Workspace` seçerek projeyi açın ve **Build Project** yapın.

### Terminal / Make ile:
```bash
make -C Debug clean
make -C Debug all
```
Çıktılar `Debug/STM32F205_Can_Bootloader.elf` ve `Debug/STM32F205_Can_Bootloader.bin` olarak üretilir.
