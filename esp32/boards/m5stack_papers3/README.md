# M5Stack PaperS3: hardware facts for ESP-IDF work

Sources, read on 2026-10-18:
- M5Stack's [PaperS3 page](https://docs.m5stack.com/en/core/PaperS3).
- M5GFX `src/M5GFX.cpp` (the PaperS3 bus config), and `src/lgfx/v1/platforms/esp32/Bus_EPD.inl`
  and `Panel_EPD.inl` (the panel driver).
- M5Unified `src/M5Unified.inl` and `src/utility/Power_Class.inl`.

The schematic was not read.

## The unit here

- ESP32-S3R8 (QFN56), rev v0.2, MAC `d0:cf:13:16:09:70`.
- 8 MB octal PSRAM in the package (AP, 3 V); 16 MB flash (ID 0x46 / 0x4018).
- USB is the chip's own USB Serial/JTAG: `/dev/cu.usbmodem*`. `idf.py flash` works as it is.
- Factory image (all 16 MB): `../../backups/m5paper_s3-d0cf13160970-factory-full.bin`
  (gitignored). To restore it:

  ```sh
  python -m esptool -p /dev/cu.usbmodem* -b 921600 write-flash 0 \
      backups/m5paper_s3-d0cf13160970-factory-full.bin
  ```

- Build and flash, then app only after the first time (this keeps NVS, i.e. the saved Wi-Fi
  networks):

  ```sh
  cd firmware/rcplayer
  idf.py -B build/m5stack_papers3 -DBOARD=m5stack_papers3 -p /dev/cu.usbmodem* flash
  idf.py -B build/m5stack_papers3 -DBOARD=m5stack_papers3 -p /dev/cu.usbmodem* app-flash
  ```

## Display: 4.7" e-ink, ED047TC1, 960x540

- **No controller chip.** The ESP32 drives the panel's source and gate drivers directly.
  - A picture is a sequence of frames.
  - Each frame scans all 540 lines.
  - In each frame, each pixel is pushed toward black, pushed toward white, or left alone.
- **The bus:** the LCD peripheral's i80 mode clocks each line out with DMA (240 bytes,
  2 bits a pixel, plus 8 bytes of padding) at 16 MHz.
  - CL (the source clock) is the i80 WR pin.
  - SPH (the source start pulse) is the i80 CS pin.
  - CKV, LE and SPV are GPIOs set around each line.
- **Pins (M5GFX's config):**
  - Data D0..D7 = 6, 14, 7, 12, 9, 11, 8, 10
  - PWR 46, SPV 17, CKV 18, SPH/XSTL 13, OE 45, LE 15, CL 16
  - M5Stack's page names G45 as PWR and leaves out OE and CL; M5GFX's assignment is the one
    that works.
- **Orientation:** used in portrait, 540x960 px with density 1.5, which gives 360 x 640 dp
  (the Tab5's layout). Portrait pixel (x, y) is panel column y, line 539 − x.
- **Waveforms:** M5GFX's tables.
  - Quality mode: an eraser (3 frames, chosen by the old grey) moves a pixel to about mid-grey,
    then 15 frames, chosen by the new grey, draw it.
  - Fast mode (`epd fast`): black and white only, 8 frames, no eraser.
  - Only pixels that changed are driven. Lines with no change get an all-"leave alone" line
    without computing it.
- **The player's frames:**
  - ARGB is converted to 16 greys with a 4x4 ordered dither. Because the dither depends only
    on the position, an unchanged picture converts to the same bytes and costs no refresh.
  - The conversion happens in `presentAsync`, so the player never waits on the panel.
  - An update task on core 0 draws the newest picture. Frames that arrive during a refresh
    replace each other.
  - A refresh takes about 0.5 s, whatever its size.
- **Ghosting:** the first picture after boot redraws every pixel. `epd clear` does the same
  later.
- **No backlight.** `setBrightness` does nothing.
- **`system.screen_eink` = 1:** the start-up screen uses a white paper palette.

## Touch: GT911

- I2C SDA 41, SCL 42, at 0x5D (it can also come up at 0x14).
- INT is 48, and there is no reset line.
- It reports 540x960, already in portrait.
- The player polls the status register (0x814E) and keeps the last report between reports.

## Power

- **Battery:** GPIO3 (ADC1 channel 2) through a 1/2 divider.
- **Charging:** GPIO4 is low while the LGS4056H charges.
- **USB:** GPIO5 (ADC1 channel 4) is above 0.2 V when USB is connected.
- **Power-off:** GPIO44 is held low. M5Unified switches the board off by pulsing it. This
  is why the console isn't on UART0, whose RX is GPIO44.
- **The power button** belongs to a PMS150G microcontroller, not to the ESP32:
  - one press switches the board on
  - a double press switches it off
  - a long press enters download mode

## Other parts on the I2C bus (not used yet)

- BM8563 RTC at 0x51
- BMI270 IMU at 0x68
- Buzzer on GPIO21
- SD card on SPI: SCK 39, MOSI 38, MISO 40, CS 47

## Console commands

- `touch` prints raw touch coordinates for 15 s.
- `i2c` lists the devices on the bus.
- `power` prints the battery voltage, level and charge state.
- `epd [clear|quality|fast]`:
  - `clear` redraws every pixel.
  - `quality` and `fast` set the waveform.
  - It also prints the update count and the time of the last update.

## Memory

- The player's two 540x960 ARGB render buffers take 4 MB of PSRAM, and the panel's three grey
  buffers take 1.5 MB.
- About 1.4 MB of PSRAM and 200 KB of internal RAM stay free with Wi-Fi up and the start-up
  screen showing.

## As a Muse gadget (firmware/muse-gadget)

> **Moved:** the Muse gadget app is part of **kobento-mcuApps** now (it needs the external Muse
> SDK + a token). kobento-mcu runs the minimal player on this board. The pairing/partition
> notes below apply to that app.

- Settings: `sdkconfig.muse-gadget.defaults` (BLE name `MuseGadget-Paper-XXXXXX`, a tap stands
  in for the setup button, the engine's allocations in PSRAM).
- The first install from rcplayer needs a full `flash` and an erased NVS (0x11000, 0xC000):
  the partition layouts differ. After that, `tools/update-muse.sh --board m5stack_papers3`
  writes the app only and keeps the pairing.
- Memory, paired and connected: about 140 KB internal and 2.8 MB PSRAM free. That needs the
  single render buffer rcstage uses on e-ink; with two, PSRAM was down to 1.2 MB before
  pairing.
