# Waveshare ESP32-S3-Touch-AMOLED-1.8: hardware facts for ESP-IDF work

Sources, read on 2026-10-19:
- Waveshare's repository `waveshareteam/ESP32-S3-Touch-AMOLED-1.8`, `examples/esp-idf/13_display_colorbar`
  (pins, panel init, the v1/v2 test) and `00_board_check`.
- arduino-esp32 `variants/waveshare_esp32_s3_touch_amoled_18/pins_arduino.h` (QSPI pins,
  touch interrupt). It has SDA/SCL swapped against Waveshare's ESP-IDF code; the ESP-IDF
  pins (SDA 15, SCL 14) are the ones that work.

The schematic was not read.

## The unit here

- ESP32-S3R8 (QFN56) rev v0.2, MAC `10:20:BA:46:49:78`: 8 MB octal PSRAM in the package
  (AP, 3.3 V), 16 MB Winbond flash (ID `ef` / `4018`), quad.
- A **v1** board: SH8601 panel and FT3168 touch (v2 has a CO5300 and a CST816 at 0x15).
- USB is the chip's own USB Serial/JTAG: `/dev/cu.usbmodem*`. `idf.py flash` works as it is.
- Factory image (an Arduino + LVGL 8 demo; app0/app1 3 MB, ffat 9.9 MB), all 16 MB:
  `../../backups/esp32s3-1020ba464978-factory-full.bin` (gitignored), SHA-256
  `387ac4704c2689946c5c1d11d5d2b1cb75ee20225e0f5566c3d3d8efec8c536c`. To restore it:

  ```sh
  python -m esptool -p /dev/cu.usbmodem* -b 921600 write-flash 0 \
      backups/esp32s3-1020ba464978-factory-full.bin
  ```

- First rcplayer install over the factory image: erase 0x9000-0xFFFF first (Arduino's
  otadata sits where rcplayer's NVS goes), then `flash`; `app-flash` after that.

## Display: 1.8" AMOLED, 368 x 448, QSPI

- **Pins:** CS 12, CLK 11, D0-D3 4, 5, 6, 7. No reset or D/C line.
- **QSPI framing** (SH8601 and CO5300 alike): a command is one line, sent as the 32-bit
  word `0x02 << 24 | cmd << 8`; pixels go on four lines after `0x32 << 24 | 0x2C << 8`.
  esp_lcd's SPI panel IO does this with `lcd_cmd_bits = 32` and `flags.quad_mode`.
- **Init** (Waveshare's): software reset, `FE 00`, `C4 80`, `3A 55` (RGB565), `35 00`,
  `53 20`, `51` brightness, `63 FF`, sleep out (120 ms), display on.
- **v2 only:** the picture starts 16 columns in (Waveshare's `V2_PANEL_X_GAP`).
- **Brightness:** register `0x51` (0-255); no backlight.
- Used at 40 MHz, in 32-line strips converted from the ARGB frame on core 0.
- **Density 1.5:** documents lay out in 245 x 299 dp.

## Touch, power and the rest (I2C: SDA 15, SCL 14)

- **Touch:** FT3168 at 0x38 (v1) or CST816 at 0x15 (v2); both keep the finger count at
  0x02 and X/Y at 0x03-0x06. Interrupt on GPIO21 (not used: the player polls).
- **Orientation verified 2026-10-19** with corner taps: raw x 0-367 left to right, y 0-447
  top to bottom, the picture's own coordinates (no flip or swap).
- **The FT3168 can miss the probe at boot** (asleep until tapped, seen once): the driver
  looks again once a second until it answers.
- **PMU:** AXP2101 at 0x34, as on the Core2 v1.1: battery mV at 0x34/0x35, fuel gauge at
  0xA4, status at 0x00/0x01. With no cell, the voltage ADC reads full scale (16.38 V).
- **Buttons:** the PWR key belongs to the AXP2101 (a short press latches 0x49 bit 3) and
  steps forward through the documents; BOOT (GPIO0) steps back. Both verified 2026-10-19.
- Also on the bus (Waveshare's examples): QMI8658 IMU 0x6B, PCF85063 RTC 0x51. ES8311
  codec for audio. None used yet.

## Console commands

`touch` (raw coordinates for 15 s), `i2c` (devices on the bus, v1/v2), `power`.
