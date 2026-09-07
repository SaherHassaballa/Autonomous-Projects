# ESP32 with Betaflight

This project allows an ESP32 board to be used with Betaflight.

## Requirements

- An ESP32 board
- The `firmware_0x00.bin` firmware file
- The [ESP Web Tools flash tool](https://espressif.github.io/esptool-js/)
- Betaflight firmware for your board
- Betaflight Configurator version 10.x
- A USB data cable

## Flashing Instructions

### 1. Flash Betaflight firmware

First, download the appropriate Betaflight `.bin` file for your board and flash it using the ESP Web Tools flash tool:

1. Open the [ESP Web Tools flash page](https://espressif.github.io/esptool-js/).
2. Connect the ESP32 to your computer with a USB data cable.
3. Select the Betaflight `.bin` file for your board.
4. Follow the tool instructions to erase and flash the board.

### 2. Flash `firmware_0x00.bin`

After flashing Betaflight, use the same tool to flash this project's firmware:

1. Keep the ESP32 connected and open the [ESP Web Tools flash tool](https://espressif.github.io/esptool-js/).
2. Select `firmware_0x00.bin`.
3. Set the flash address to **`0x00`**.
4. Click **Program** and wait for the process to finish successfully.
5. Disconnect the ESP32 after flashing is complete.

> Important: The firmware must be flashed at address `0x00`.

## Connecting with Betaflight

1. Open Betaflight Configurator **10.x**.
2. Connect the ESP32 to your computer again.
3. Select the correct serial port and click **Connect**.
4. Configure the Betaflight options as shown in the project image or video.

Newer Betaflight Configurator versions may not support this ESP32 feature. Use Betaflight 10.x if the board is not detected or the feature is unavailable.

## Downloads and References

- [ESP Web Tools flash page](https://espressif.github.io/esptool-js/)
- [ESP-FC firmware releases](https://github.com/rtlopez/esp-fc/releases)
- [Setup video](https://www.youtube.com/watch?v=v9hY0CBVKWs&t=165s)

## Notes

- Use a USB cable that supports data transfer.
- Do not disconnect the board while flashing is in progress.
- Make sure the selected firmware matches your ESP32 board.
