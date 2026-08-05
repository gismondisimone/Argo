# BU03-Kit remote firmware

`BU03_remote_controller.c` is custom STM32 firmware for the Ai-Thinker BU03-Kit. It does not use a Raspberry Pi or the factory AT firmware.

## Wiring

All connections must use 3.3 V logic.

| ST7789 pin | BU03-Kit pin |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SCL/SCK | PB13 |
| SDA/MOSI | PB15 |
| CS | PB12 |
| DC | PA1 |
| RES/RST | PC13 |
| BL/BLK | 3V3 |

Connect buttons 1 through 6 respectively between `PB8`, `PB9`, `PB10`, `PB11`, `PA2`, `PA3` and GND. Internal pull-ups are enabled, so no external resistors are required.

## Packet protocol

Each button sends a channel-5, 6.8-Mbps UWB frame with type `0xA1` and a one-byte payload: `01` to `06`. Buttons 1–4 therefore send `01`–`04`.

The remote displays packets that use type `0xA2`; the bytes after that type are shown in hexadecimal. For example, the rover can return `A2 12 34 56`, which appears as `12 34 56` on the TFT.

The rover must use the same channel, UWB configuration, PAN ID `0xCADE`, and packet format. This is a new direct-radio protocol; it is not compatible with the factory AT ranging/positioning firmware.

## Build and flash

1. Download the official [STM32F103-BU0x SDK](https://gitee.com/Ai-Thinker-Open/STM32F103-BU0x_SDK).
2. Replace `Components/Main/main.c` in that SDK with `BU03_remote_controller.c`.
3. Open `Projects/USER/Project.uvprojx` in Keil MDK and select the `STM32F103C8` target.
4. Build it, then flash the generated `.hex` through the board’s SWD header with an ST-Link. Preserve a copy of the original factory firmware first if you may want to restore its AT/ranging behavior.

The stock SDK driver owns SPI1 and the pins connected to the BU03 module. The TFT uses SPI2 so it does not conflict with the UWB radio.
