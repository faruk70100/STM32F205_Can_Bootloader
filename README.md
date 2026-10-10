# STM32F205 CAN Bootloader & Test Project 🛡️

A production-ready secondary bootloader and test firmware implementation for the **STM32F205** microcontroller, enabling safe firmware updates and remote management over a **CAN (Controller Area Network)** bus.

---

## 📋 Features Overview

* **High-Speed CAN Communication:** Operates reliably on a 1 Mbit/s CAN 2.0A bus using standard 11-bit IDs.
* **Ring Buffer Architecture:** Asynchronous 64-packet (512 Byte) ring buffer preventing flash blocking inside ISRs.
* **Dynamic Sector Management:** Automatically erases only the flash sectors required based on the incoming firmware size.
* **Integrity & Verification:** Implements software-based CRC32 validation before executing the application.
* **Progress Tracking:** Sends periodic ACK progress updates (every 32 data packets) back to the Host.
* **Remote Bootloader Trigger:** Allows jumping back to the bootloader mode via a special CAN command frame without cycling device power.
* **Seamless Application Jump:** Cleanly clears SysTick, CAN peripherals, updates vector table offsets (`SCB->VTOR`), and transfers execution control safely.

---

## 🛠️ Hardware & Technical Specifications

* **Microcontroller:** STM32F205RCT6 (ARM® Cortex®-M3, 120 MHz SYSCLK)
* **Flash Memory:** 256 KB total (Partitioned into Bootloader and Application regions)
* **RAM:** 128 KB (80 KB SRAM1 + 16 KB SRAM2 for magic word flags)
* **Transceiver / Clock:** External 12 MHz crystal (HSE) with PLL configured to 120 MHz.

---

## 📐 Memory Map

| Region | Sector Range | Address Range | Size | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Bootloader** | Sector 0 - 1 | `0x08000000` - `0x08007FFF` | 32 KB | Primary bootloader and flash routines. |
| **Application** | Sector 2 - 5 | `0x08008000` - `0x0803FFFF` | 224 KB | Main user firmware / application space. |

---

## 🚀 Quick Start & Getting Started

1. **Clone the repository:**
   ```bash
   git clone [https://github.com/faruk70100/STM32F205-CAN-Bootloader.git](https://github.com/faruk70100/STM32F205-CAN-Bootloader.git)
