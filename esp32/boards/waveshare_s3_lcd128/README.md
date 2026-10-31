# Waveshare ESP32-S3 1.28" round: hardware facts for ESP-IDF work

Sources, read on 2026-10-18: Waveshare's wiki pages
[ESP32-S3-LCD-1.28](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.28) and
[ESP32-S3-Touch-LCD-1.28](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28). The demo
ZIPs and schematics linked there were not read.

## The unit here

- ESP32-S3R2 (QFN56), rev v0.2, MAC `84:fc:e6:52:18:c4`.
- 2 MB quad PSRAM in the package; 16 MB W25Q128 flash (ID 0xEF / 0x4018).
- USB is a CH343P bridge on UART0 (GPIO43/44): `/dev/cu.usbmodem*`, VID 0x1A86, PID 0x55D3.
- Factory firmware: Waveshare's "LVGL_Arduino" demo (it prints "Hello Arduino! V8.3.10").
- **Flashing:** `idf.py flash` fails on this unit. Through the CH343P on macOS's built-in
  CDC driver, packets of about 512 bytes and more arrive corrupted, at any baud rate:
  - The stub flasher can't be uploaded ("Failed to write to target RAM (result was 0107:
    Checksum error)").
  - ROM writes with esptool's 1 KB blocks fail too ("result was 0105: The format of the
    received message is invalid").
  - 256-byte blocks work. `tools/esptool256.py` is esptool with that block size, at about
    190 kbit/s (a 3 MB app in about 2 minutes). Reads work with plain `--no-stub`.

  ```sh
  cd firmware/rcplayer && idf.py -B build/waveshare_s3_lcd128 -DBOARD=waveshare_s3_lcd128 build
  PY=~/.espressif/python_env/idf6.0_py3.14_env/bin/python
  # first time (bootloader, partition table, app):
  (cd build/waveshare_s3_lcd128 && $PY ../../../../tools/esptool256.py --chip esp32s3 \
      -p /dev/cu.usbmodem* -b 460800 --no-stub write-flash @flash_args)
  # after that, the app only (keeps NVS: the saved Wi-Fi networks):
  $PY ../../tools/esptool256.py --chip esp32s3 -p /dev/cu.usbmodem* -b 460800 --no-stub \
      write-flash 0x10000 build/waveshare_s3_lcd128/rcplayer.bin
  ```

  WCH's own macOS driver (CH34XSER_MAC, linked from the wiki) may avoid this; it hasn't been
  tried.
- **Factory image:** 0x0–0x290000 (bootloader, partition table, NVS, both app slots) is in
  `../../backups/` (gitignored). To restore it (with tools/esptool256.py as above):

  ```sh
  $PY tools/esptool256.py --chip esp32s3 -p /dev/cu.usbmodem* -b 460800 --no-stub \
      write-flash 0 backups/waveshare_s3_lcd128-84fce65218c4-factory-0-290000.bin
  ```

## Two variants, told apart at boot

The touch variant has a CST816S at I2C 0x15. It answers only after a reset on GPIO13, so
`board.cpp` pulses that pin and then probes. The LCD reset and backlight pins differ:

| | ESP32-S3-LCD-1.28 | ESP32-S3-Touch-LCD-1.28 |
|---|---|---|
| LCD reset | GPIO12 | GPIO14 (GPIO12 is MISO) |
| Backlight | GPIO40 | GPIO2 |
| Touch | none | CST816S: SDA 6, SCL 7, INT 5, RST 13 |
| IMU QMI8658 interrupts | INT1 47, INT2 48 | INT1 4, INT2 3 |
| Extra I/O | 1.27 mm headers | 6-pin SH1.0 (15, 16, 17, 18, 21, 33); MOSFET pads on 4 and 5 |

## Shared pins

- **LCD (GC9A01A, 240x240, round):** SCLK 10, MOSI 11, CS 9, DC 8, on SPI2.
  - The init is Waveshare's `LCD_1in28` sequence, with MADCTL 0x08 and INVON.
  - The pins go through the GPIO matrix (SPI2's IO_MUX clock pin is 12), so the clock is
    40 MHz. Waveshare says the panel takes 80 MHz.
- **I2C:** SDA 6, SCL 7. Holds the touch chip (0x15) and the QMI8658 IMU (0x6B, not yet
  used).
- **Battery:** GPIO1 (ADC1 channel 0) through a 200k/100k divider, so V = 3 × the pin
  voltage. The charger is an ETA6096 with no status line, so the charge state is unknown and
  the level comes from the voltage. With no cell attached the pin reads the charger's output
  (4.64 V seen on USB), so anything above 4.4 V means "no battery, on USB power".
- **Buttons:** BOOT is GPIO0; RESET is the chip's EN.

## Memory

- 2 MB PSRAM.
- With `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0` (sdkconfig.rcplayer.defaults), 165 KB of
  internal RAM stays free with Wi-Fi up and a document showing.
- With the app default (4096), the engine's small allocations had filled internal RAM to
  24 KB before Wi-Fi connected.

## Console commands

- `touch` prints raw touch coordinates for 15 s.
- `i2c` lists the devices on the bus and the detected variant.
- `power` prints the battery voltage and level.

## Screen

The round face shows the inscribed circle of the 240x240 frame. The board sets
`system.screen_round` = 1, and documents that read it (the start-up screen) use their round
layout. Square layouts lose their corners.
