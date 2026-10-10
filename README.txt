================================================================================
STM32F205 CAN BOOTLOADER & UYGULAMA TEST PROJESI
================================================================================
Mikrodenetleyici : STM32F205RCT6 (ARM Cortex-M3, 120 MHz SYSCLK)
Hafıza           : 256 KB Flash, 128 KB RAM (SRAM1: 80 KB + SRAM2: 16 KB)
İletişim         : CAN 2.0A (1 Mbit/s, Standart 11-bit ID)
Geliştirme Ortamı: STM32CubeIDE / GNU ARM GCC
================================================================================

1. GENEL BAKIS VE ÖZELLIKLER
--------------------------------------------------------------------------------
Bu proje, STM32F205 serisi mikrodenetleyiciler için optimize edilmiş güvenilir,
hızlı ve kesintisiz (uzaktan tetiklenebilen) bir CAN Bootloader ve test yazılımıdır.

Öne Çıkan Özellikler:
* Tek Zamanlayıcı (TIM3) ile hassas zaman tabanı (1 us sayma adımı, 1 ms periyot).
  Herhangi bir timer kayması veya donanım çakışması yaşanmaz.
* 1 Mbit/s CAN Bus haberleşmesi (6 TQ zamanlama: 1 TQ Sync + 2 TQ BS1 + 2 TQ BS2).
* Kesme (ISR) içinde flash bloklaması yapmayan 64 paketlik (512 Byte) Ring Buffer mimarisi.
* Otomatik sektör silme (Firmware boyutuna göre sadece gereken sektörleri siler).
* Donanım yazma sonrası yazılımsal CRC32 doğrulama mekanizması.
* Her 32 veri paketinde bir Host'a ilerleme (ACK Progress - 0x704) bildirimi.
* Uzaktan Bootloader Moduna Geçiş (0x7F0): Cihazın enerjisini kesmeden, uygulama
  çalışırken CAN üzerinden özel bir imza paketi göndererek bootloader'a alma desteği.
* Güvenli Uygulamaya Atlama (JumpToApplication): MSP kontrolü, kesmelerin, 
  SysTick ve CAN çevrebiriminin temizlenmesi ve SCB->VTOR vektör tablosu taşıma.


2. DONANIM VE PIN BAGLANTILARI
--------------------------------------------------------------------------------
Kristal (HSE) : 12 MHz Harici Kristal (PH0-OSC_IN, PH1-OSC_OUT)
                PLL Yapılandırması: M=12, N=240, P=2, Q=4 -> SYSCLK = 120 MHz
                APB1 Prescaler = 4 (30 MHz, Timer Clock = 60 MHz)
                APB2 Prescaler = 2 (60 MHz, Timer Clock = 120 MHz)

CAN1 Hattı    : PA11 -> CAN1_RX (CAN Alıcı-Verici RX pini)
                PA12 -> CAN1_TX (CAN Alıcı-Verici TX pini)
                Baudrate: 1.000.000 bps (1 Mbps)

Durum LED'leri:
* PB10 -> LED_Can : CAN Canlılık & Haberleşme LED'i (Her test adımında blink yapar)
* PB11 -> LED_PWM : 1. Durum / Test LED'i (Adım 3, 4, 5 testlerinde aktif)
* PB9  -> LED_Pin : 2. Durum / Tekrarlanabilirlik LED'i (Adım 6 ve 7 testlerinde aktif)

Zamanlayıcı   :
* TIM3 -> Tek donanımsal timer olarak yapılandırılmıştır.
          APB1 Timer Saati = 60 MHz
          Prescaler = 59 (Sayıcı = 1 MHz, her tick = 1 mikrosaniye)
          Period    = 999 (Sayıcı periyodu = 1000 us = 1 milisaniye)


3. FLASH VE BELLEK HARITASI (MEMORY MAP)
--------------------------------------------------------------------------------
STM32F205RCT6'nın 256 KB Flash hafızası sektörlere göre şu şekilde paylaştırılmıştır:

Bölge        Sektörler    Flash Adres Aralığı        Boyut   Kullanım Amacı
--------------------------------------------------------------------------------
Bootloader   Sector 0..1  0x08000000 - 0x08007FFF    32 KB   CAN Bootloader Kodu
Uygulama     Sector 2     0x08008000 - 0x0800BFFF    16 KB   Kullanıcı Firmware
             Sector 3     0x0800C000 - 0x0800FFFF    16 KB   Kullanıcı Firmware
             Sector 4     0x08010000 - 0x0801FFFF    64 KB   Kullanıcı Firmware
             Sector 5     0x08020000 - 0x0803FFFF   128 KB   Kullanıcı Firmware
             Toplam Uygulama Alanı                 : 224 KB (0x08008000 - 0x0803FFFF)

RAM Paylaşımı:
* SRAM1 (0x20000000, 80 KB) : Yığın (Stack), Statik Değişkenler ve Kuyruklar
* SRAM2 (0x2001C000, 16 KB) : 0x2001C000 adresinde "Magic Word" (0xB007B007) tutulur.
  Uygulama uzaktan bootloader isteği aldığında bu adrese bayrağı yazar ve NVIC reset atar.
  Sistem açılışında bootloader bu adresi kontrol ederek 30 sn Host'u bekler.


4. CAN HABERLESME PROTOKOLÜ (HOST <-> STM32F205)
--------------------------------------------------------------------------------
Standart 11-bit CAN ID'leri kullanılmaktadır:

CAN ID  Yön           DLC    Açıklama / Veri Yükü (Payload)
--------------------------------------------------------------------------------
0x700   Host -> MCU   5      Flash Erase & Güncelleme Başlat
                             Byte 0: 0x01
                             Byte 1..4: Firmware Toplam Boyutu (uint32 Little-Endian)

0x701   Host -> MCU   1..8   Firmware Veri Paketi
                             Byte 0..7: Flash'a yazılacak ikili veri

0x702   Host -> MCU   1      Güncelleme Tamamlandı (Transfer Finish)
                             Byte 0: 0x01

0x703   MCU -> Host   1      Erase ACK Yanıtı
                             Byte 0: 0x01 = Başarılı, 0x00 = Erase Hatası

0x704   MCU -> Host   4      İlerleme Bildirimi (ACK Progress - Her 32 pakette bir)
                             Byte 0..3: Yazılan toplam byte miktarı (uint32 LE)

0x705   MCU -> Host   4      CRC32 Doğrulama Paketi
                             Byte 0..3: Yazılan alanın CRC32 sonucu (uint32 LE)

0x706   MCU -> Host   2      Hata Bildirimi
                             Byte 0: Hata Kodu (1: Erase, 2: Program, 3: Overflow, 4: Sync)
                             Byte 1: Hata Detayı

0x707   MCU -> Host   1      Boşta Bekleme Geri Sayımı
                             Byte 0: Kalan saniye (Örn: 3, 2, 1)

0x710   Host -> MCU   2      Uygulama Modu: LED Blink Periyodu Ayarı
                             Byte 0..1: Yeni periyot (uint16 LE ms, 50 - 5000 ms)

0x720   MCU -> Host   8      Uygulama Modu: Canlılık / Telemetri Paketi (Periyodik 500 ms)
                             Byte 0..3: Gönderim sayacı (uint32 LE)
                             Byte 4..5: Mevcut LED periyodu (uint16 LE ms)
                             Byte 6: 0xAA (Canlı / Tamam durumu)
                             Byte 7: 0x55 (Kontrol baytı)

0x7F0   Host -> MCU   8      Uzaktan Bootloader'a Giriş Tetikleme
                             Payload: 0xDE 0xAD 0xC0 0xDE 0xB0 0x07 0x13 0x37


5. ADIM ADIM TEST REHBERI
--------------------------------------------------------------------------------
Kullanıcının projeyi adım adım doğrulayabilmesi için 7 aşamalı test planı:

--------------------------------------------------------------------------------
[ADIM 1 & 2] : TEMEL BOOTLOADER VE DONANIM DOGRULAMA (ST-LINK ILE)
--------------------------------------------------------------------------------
Amaç:
MCU'nun donanım olarak çalıştığını, kristalin (120 MHz) ve CAN LED'inin sağlam
olduğunu teyit etmek.

Hangi Dosyada Ne Yapılacak?
1) Core/Src/main.c:
   -> #define STEP_TEST_1_2        satırının yorumunu AÇIN.
   -> #define BOOTLOADER_BUILD     satırının yorumunu AÇIN.
   -> Diğer STEP_TEST_x satırlarını YORUMA ALIN.
2) STM32F205RCTX_FLASH.ld:
   -> FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 32K satırını AÇIN.
   -> 0x08008000 satırını YORUMA ALIN.

Beklenen Davranış:
* Karta ST-Link ile kod atılır.
* Sadece PB10 (LED_Can) yanıp söner. PB11 ve PB9 tamamen sönük kalır.
* 3 saniye boyunca CAN'dan ID:0x707 ile geri sayım basar. Geçerli app yoksa bekler.

--------------------------------------------------------------------------------
[ADIM 3, 4, 5] : CAN ÜZERINDEN FIRMWARE ATMA & 1. LED TESTI
--------------------------------------------------------------------------------
Amaç:
CAN Bootloader üzerinden ilk kullanıcı uygulamasının (.bin) flash'a yazılıp
uygulamaya başarıyla zıplandığını doğrulamak.

Hangi Dosyada Ne Yapılacak?
1) Core/Src/main.c:
   -> #define STEP_TEST_3_5        satırının yorumunu AÇIN.
   -> #define BOOTLOADER_BUILD     satırını MUTLAKA YORUMA ALIN!
   -> Diğer STEP_TEST_x satırlarını YORUMA ALIN.
2) STM32F205RCTX_FLASH.ld:
   -> FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 224K satırını AÇIN.
   -> 0x08000000 satırını YORUMA ALIN.

Beklenen Davranış:
* Üretilen .bin dosyası CAN arayüzü (Host) ile MCU'ya gönderilir.
* Bootloader flash'ı siler, veriyi yazar, CRC32 onaylar ve uygulamaya zıplar.
* PB10 (LED_Can) ile PB11 (LED_PWM) birlikte senkron yanıp söner. PB9 sönüktür.

--------------------------------------------------------------------------------
[ADIM 6] : TEKRARLANABILIRLIK TESTI (CAN ÜZERINDEN 2. LED TESTI)
--------------------------------------------------------------------------------
Amaç:
Flash sektörlerinin tekrar silinip yeni bir firmware'in başarıyla yazılabildiğini teyit etmek.

Hangi Dosyada Ne Yapılacak?
1) Core/Src/main.c:
   -> #define STEP_TEST_6_LED2     satırının yorumunu AÇIN.
   -> #define BOOTLOADER_BUILD     satırını YORUMA ALIN.
2) STM32F205RCTX_FLASH.ld:
   -> FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 224K olmalıdır.

Beklenen Davranış:
* CAN üzerinden yeni üretilen .bin atılır.
* Yükleme bitince PB10 (LED_Can) ile PB9 (LED_Pin) yanıp söner. PB11 sönük kalır.

--------------------------------------------------------------------------------
[ADIM 7] : CAN VERI AKISI, HIZ KONTROLÜ VE UZAKTAN BOOTLOADER TETIKLEME
--------------------------------------------------------------------------------
Amaç:
CAN üzerinden telemetri almak, LED periyodunu CAN'dan değiştirmek ve güç kesmeden
bootloader moduna geçebilmek.

Hangi Dosyada Ne Yapılacak?
1) Core/Src/main.c:
   -> #define STEP_TEST_7_CAN_FLOW satırının yorumunu AÇIN.
   -> #define BOOTLOADER_BUILD     satırını YORUMA ALIN.
2) STM32F205RCTX_FLASH.ld:
   -> FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 224K olmalıdır.

Beklenen Davranış:
* Cihaz her 500 ms'de bir CAN hattına ID:0x720 telemetri basar.
* Host ID:0x710 ile örneğin [0x32, 0x00] (50 ms) yollarsa LED flaş hızı artar.
* Host ID:0x7F0 ile [0xDE, 0xAD, 0xC0, 0xDE, 0xB0, 0x07, 0x13, 0x37] yollarsa
  cihaz reset atar ve 30 saniye boyunca CAN Bootloader modunda Host'u bekler!


6. PROJEYI DERLEME VE YÜKLEME
--------------------------------------------------------------------------------
STM32CubeIDE İçinde:
1) Projeyi IDE'ye File -> Import -> Existing Projects into Workspace ile aktarın.
2) Proje Gezgini'nde projeye sağ tıklayıp "Build Project" seçin.
3) Çıktılar:
   - Debug/STM32F205_Can_Bootloader.elf
   - Debug/STM32F205_Can_Bootloader.bin
   otomatik olarak üretilir.

Komut Satırından / Make İle Derleme:
   make -C Debug clean
   make -C Debug all
================================================================================
