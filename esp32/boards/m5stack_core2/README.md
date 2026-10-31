# M5Stack Core2: hardware facts for ESP-IDF work

These facts come from M5Stack's own code, read on 2026-10-18:
- M5Unified: `Power_Class.cpp`, `power/AXP192_Class.cpp`, `power/AXP2101_Class.cpp`,
  `power/INA3221_Class.cpp`, `M5Unified.cpp`
- M5GFX: `M5GFX.cpp` (board_M5StackCore2), `lgfx/v1/panel/Panel_ILI9342.hpp`,
  `lgfx/v1/touch/Touch_FT5x06.cpp`

The Muse Gadget SDK's own Core2 support is `components/muse/boards/board_m5stack_core2.c`,
together with `devices/sdkconfig.muse-m5stack-core2`. It uses Espressif's `m5stack_core_2`
BSP and supports **v1.0 only**.

## The unit here

- ESP32-D0WDQ6-V3, rev 3.0, MAC `78:21:84:93:41:9c`
- 16 MB flash (ID 0x20 / 0x4018)
- USB serial on `/dev/cu.usbserial-*` (CH9102)
- **Flash speed:** reads at 921600 and 460800 baud failed through the bridge, and 115200
  works. Writes at the default 460800 are fine.
- **Factory image:** a full-flash backup is in `../../backups/` (gitignored). To restore it:

  ```sh
  esptool --port /dev/cu.usbserial-* write-flash 0 backups/m5stack_core2-784193419c-factory.bin
  ```

## Running it

In kobento-mcu this board runs the minimal player — the embedded demo documents, stepped by
the power key or the serial console (see the repo [README](../../../README.md)). The three
printed marks below the screen are rows 240–279 of the touch panel (`touchMarkStep` in
`display.cpp`): a document that declares `Control.next` / `Control.back` receives them (and
the power key) as button presses instead of them stepping documents.

The Home Assistant thermostat demo that used to run here (Wi-Fi + `POST /named` + climate
set-points over the dial and ± keys) moved to **kobento-mcuApps** (`esp32/firmware/rcplayer`),
which builds on this library.

## Two revisions, told apart at boot

Read PMU register **0x03** at I2C address 0x34:

| Value | Revision | PMU | Notes |
|---|---|---|---|
| 0x03 | v1.0 | AXP192 | |
| 0x4A | v1.1 | AXP2101 | plus an INA3221 current monitor at 0x40 |

`components/board/src/power.cpp` handles both.

| | v1.0 (AXP192) | v1.1 (AXP2101) |
|---|---|---|
| LCD and touch reset | GPIO4, reg 0x96 bit 1 | ALDO2, reg 0x90 bit 1 |
| LCD logic and SD power | LDO2, 3.3 V | ALDO4 |
| Backlight | DCDC3: reg 0x27 = (b >> 3) + 72; enable 0x12 bit 1 | BLDO1: reg 0x96 = (b + 641) >> 5; enable 0x90 bit 4 |
| Speaker amp | GPIO2, reg 0x93: 0x05 = off | ALDO3 |
| Vibration motor | LDO3 | DLDO1 |
| Battery voltage | 0x78, 12 bits × 1.1 mV | 0x34, 14 bits, in mV |
| Battery current | 0x7A charge, 0x7C discharge, 13 bits × 0.5 mA | INA3221 CH1: shunt × 5 µV / 10 mΩ |
| Battery level | from voltage: (mV − 3300) / 8 | fuel gauge, reg 0xA4 |
| Charging | reg 0x00 bit 2 | reg 0x01 bits 6:5 = 01 |

**Do not copy M5GFX's reset on v1.1.** M5GFX toggles AXP192 register 0x96 bit 1 for the LCD
reset on both revisions. On the AXP2101, 0x96 is the backlight voltage register.

## Buses and pins

**Internal I2C** (I2C_NUM_1): SDA 21, SCL 22, 400 kHz.

| Address | Device |
|---|---|
| 0x34 | PMU |
| 0x38 | FT6336U touch |
| 0x51 | BM8563 RTC |
| 0x68 | MPU6886 IMU |
| 0x40 | INA3221, v1.1 only |

**LCD:** ILI9342C on SPI3.
- **Pins:** MOSI 23, MISO 38, SCLK 18, CS 5, DC 15. Reset is through the PMU (see above).
- **Clocks:** writes at 40 MHz, reads at 16 MHz.
- **Mode:** 320x240 landscape, INVON, RGB565, MADCTL 0x08 (BGR). That is M5's standard
  orientation, with the touch strip for the three buttons below the screen. Units without
  printed button marks have a blank bezel there, so hold the board with that strip at the
  bottom. `CONFIG_CORE2_ROTATE_180` turns both the picture (MADCTL 0xC8) and touch around
  (verified by eye).
- **No Memory Write Continue:** this panel ignores 0x3C. Each strip needs its own RASET
  plus RAMWR (0x2C), or only the first strip is drawn (verified).
- **SD card:** shares the bus with CS on GPIO4. Keep it high.
- **Init sequence:** in `display.cpp`, copied from M5GFX's `Panel_ILI9342`.
- **DMA:** the classic ESP32's SPI DMA can't read PSRAM, so frames are converted to RGB565 in
  two 16-line strips in internal RAM.

**Touch:** FT6336U, INT GPIO39.
- **Orientation:** raw coordinates match the standard orientation. Tapping the top-left of the
  picture reads about (26, 30), and the blank bezel below the screen reads rows 240–279. With
  `CONFIG_CORE2_ROTATE_180` they become x' = 319 − x and y' = 239 − y.
- **Range:** x 0–319, y 0–279. Rows 240–279 are the three printed button marks below the
  screen (BtnA/B/C = x × 3 / 320).
- **Init:** write 0x00 = 0; read 0xA3..0xA8 (vendor ID must not be 0); write 0xA4 = 0 to
  poll.
- **Reading:** reg 0x02 gives the count, and each point is 6 bytes.

**Audio:**
- Speaker: NS4168 on I2S, BCK 12, WS 0, DATA 2.
- Microphone: PDM, CLK 0, DATA 34.

**Ports:** A is 32/33 (I2C_NUM_0), B is 36/26, C is 13/14.
