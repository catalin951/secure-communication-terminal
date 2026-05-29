# Secure Communication Terminal 📻🔒

A fully digital, hardware-secured wireless communication device based on the ESP32 microcontroller. Unlike a classic analog radio station, this device allows real-time voice capture, P2P transmission, and playback **only** if the user passes a biometric authentication filter. 

The system utilizes an ESP32 for processing, an AS608 fingerprint sensor for Zero-Trust access control, and dedicated I2S peripherals for pure digital audio transmission via the ESP-NOW protocol.

## ⚠️ CRITICAL SECURITY CONFIGURATION ⚠️

If you are cloning this repository to build your own terminals, you **MUST** update the following network and security variables in `src/main.cpp` before flashing the code:

1. **Target MAC Address (`peerAddress`):** ESP-NOW requires the exact physical MAC address of the destination device. You must change the `peerAddress[]` array in the code of "Terminal A" to match the MAC of "Terminal B", and vice versa.
2. **AES-128 Secret Key (`secretKey`):** The system uses ESP-NOW's native MAC-layer encryption. You **MUST** change the default 16-byte `secretKey[16]` array to your own random hex values. This key acts as your Pre-Shared Key (PMK) and must be identical on both devices to establish a secure link. Do not use the default repository key!

## Hardware Requirements

To build one terminal, you need the following components:
* 1x **ESP32 DevKit V1** (WROOM-32)
* 1x **AS608** Optical Fingerprint Sensor (UART)
* 1x **INMP441** Digital Omnidirectional Microphone (I2S)
* 1x **MAX98357A** Class D Audio Amplifier (I2S)
* 1x **SSD1306** 0.96" Monochrome OLED Display (I2C)
* 1x 1W Mini Speaker (Analog)
* 3x Tactile Push Buttons (PTT, Admin, Reset)
* 1x Battery Holder (4x AA slots) + 4x NiMH 1.2V Rechargeable Cells
* Custom Enclosure / Breadboard Assembly

## Pinout Mapping

| Component | ESP32 Pin | Signal Type |
| :--- | :--- | :--- |
| **Power (VIN)** | `VIN` | 4.8V Raw Power (Amp & ESP32) |
| **AS608 (RX/TX)** | `D16` / `D17` | UART2 |
| **INMP441 (WS/SCK/SD)** | `D33` / `D18` / `D32` | I2S0 (Microphone) |
| **MAX98357A (WS/BCLK/DIN)** | `D26` / `D27` / `D14` | I2S1 (Amplifier) |
| **SSD1306 (SDA/SCL)** | `D21` / `D22` | I2C |
| **PTT Button** | `D13` | GPIO (INPUT_PULLUP) |
| **Reset Button** | `D19` | GPIO (INPUT_PULLUP) |
| **Admin Button** | `D23` | GPIO (INPUT_PULLUP) |

## Access Control & FSM Logic

The terminal operates based on a strict Finite State Machine (FSM):
* **ST_LOCKED:** The device is isolated. The amplifier is muted (injecting digital zeroes to prevent hiss), and the system waits for biometric validation. Incoming transmissions trigger a hardware beep but audio is blocked.
* **ST_UNLOCKED:** Reached after a valid General User fingerprint match. Unlocks full Two-Way communication for a 2-minute session.
* **ST_ADMIN:** Triggered exclusively by Fingerprint ID 1. Allows enrollment of new users (short press) or complete database wipe (long press).
* **ST_TRANSMIT:** Half-Duplex mode. Pushing the PTT button samples 32-bit audio from the INMP441, truncates it to 16-bit to maximize the ESP-NOW payload (240 bytes), and streams it over the encrypted link. 

## Development & Compilation

This project is built using **PlatformIO**. 
1. Clone the repository.
2. Open the project folder in VS Code with the PlatformIO extension installed.
3. Update the MAC addresses and AES keys.
4. Compile and upload to your ESP32 boards using `Upload`.
5. Note: The project leverages the `driver/i2s_std.h` API from the ESP-IDF natively within the Arduino framework.

## Additional Links:
1. Page with more information about the project: https://ocw.cs.pub.ro/courses/pm/prj2026/theodor_ioan.buliga/catalin.manole1211
2. Demo of the project: https://www.youtube.com/shorts/ny9w-C_flPQ
## License

This project is licensed under the [MIT License](LICENSE) - see the LICENSE file for details.
