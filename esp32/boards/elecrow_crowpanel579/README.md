# Elecrow CrowPanel ESP32 5.79" E-paper: hardware facts for ESP-IDF work

Sources, read on 2026-10-23:
- Elecrow's repository
  [CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792):
  the factory source (`factory_sourecode/5.79_tow/main`) and the schematic's net names.
- The factory firmware on this unit, which names itself `CrowPanel5-79`.

## The unit here

- ESP32-S3-WROOM-1-N8R8: ESP32-S3 rev v0.2, MAC `44:b1:76:c8:dc:f0`, 8 MB quad flash
  (ID 0x20 / 0x4017), 8 MB octal PSRAM.
- USB goes through a CH340 to UART0: `/dev/cu.usbserial-*`, 115200.
  - esptool's 921600 baud is unreliable through it: flash at 460800, read at 230400.
- Factory image (all 8 MB): `../../backups/crowpanel579-44b176c8dcf0-factory-full.bin`
  (gitignored; sha256 `105d8467…04fc`). To restore it:

  ```sh
  python -m esptool -p /dev/cu.usbserial-* -b 460800 write-flash 0 \
      backups/crowpanel579-44b176c8dcf0-factory-full.bin
  ```

- Build and flash, then app only after the first time (keeps NVS):

  ```sh
  cd firmware/rcplayer
  idf.py -B build/elecrow_crowpanel579 -DBOARD=elecrow_crowpanel579 -p /dev/cu.usbserial-* -b 460800 flash
  idf.py -B build/elecrow_crowpanel579 -DBOARD=elecrow_crowpanel579 -p /dev/cu.usbserial-* -b 460800 app-flash
  ```

- `tools/snap.py --port /dev/cu.usbserial-* --baud 115200 ...` talks to it.

## Pins

| What | GPIO | Notes |
|---|---|---|
| Panel SPI SCK, MOSI | 12, 11 | SPI2, 10 MHz here (Elecrow bit-bangs it) |
| Panel CS, DC, RST, BUSY | 45, 46, 47, 48 | BUSY high while busy |
| Panel power | 7 | high = on |
| Power LED | 41 | high = on |
| Dial up, down, press | 6, 4, 5 | low while pressed |
| MENU, EXIT | 2, 1 | low while pressed; Elecrow wakes from deep sleep on MENU |
| TF card MOSI, MISO, SCK, CS, power | 40, 13, 39, 10, 42 | not used yet |

## The panel

- 792 x 272, black and white, two SSD1683 controllers of 400 sources each, sharing CS.
  The second answers the same commands with bit 7 set (0x24 → 0xA4, 0x11 → 0x91, ...).
- The driver keeps 800 columns with an 8-column gap at the seam (logical x ≥ 396 moves
  right by 8). Data goes column by column, 272 rows each; the second controller's RAM runs
  right to left. All of this follows Elecrow's `EPD_Display`.
- Updates use the controllers' own waveforms:
  - full, `22 F7`: about 1.9 s, flashes;
  - partial, `22 FF` with the previous picture in the old RAM (0x26/0xA6): about 0.6 s.
- The player's frames are dithered to black and white (4x4 ordered). Full refreshes come at
  start, after a key switches documents, when more than half the panel changes, and after
  60 partial updates (ghosting); everything else is partial, so a third of the panel can
  change every second.
- No grey levels yet: Elecrow's code has none either.

## The player on it

> **Moved:** the Home Assistant / IoT / wide documents, the `CONFIG_RCPLAYER_DOCUMENTS`
> selection and the HA client described below are part of **kobento-mcuApps** now. In
> kobento-mcu this board runs the minimal player — the demo documents, stepped by the keys.
> The `kDensity`, dithering and key facts here still apply.

- `kDensity` 1.0: documents lay out in 792 x 272 dp.
- Built in, chosen by `CONFIG_RCPLAYER_DOCUMENTS` in `sdkconfig.rcplayer.defaults` (the
  portrait documents don't suit this strip):
  - `ha_thermostats`, shown at start: the three thermostats of Home Assistant's demo
    integration (`climate.heatpump`, `climate.hvac`, `climate.ecobee`), live: name, mode,
    current temperature, set point or range, what it's doing, fan mode. Made by
    `tools/ha/make_ha_thermostats.py`; it reads the `ha.*` named variables that rcplayer's
    Home Assistant client fills (below). "Waiting for Home Assistant…" until the first poll.
  - `iot_cycle`: all 80 light IoT panels, frozen, three at a time; every
    second one slot takes the next panel (slot 0, 1, 2, 0, ...), driven by the document's
    own `timeInSec`. Made by `tools/iot/make_iot_trio.py --cycle`.
  - `iot_trio`: three of the light IoT panels from RemoteCompose-Examples
    (thermostat, smart lock, home battery) side by side, their animation frozen. Made by
    `tools/iot/make_iot_trio.py`; give it three other names from
    `documents/iot_light_src/` for another set. It draws once and never refreshes.
  - `wide_welcome`, `wide_forecast` and `wide_agenda` (`documents/wide/`, made by
    `tools/wide/make_wide.py`).
- Light colours (the panels' pale orange and grey) dither to sparse dots in black and
  white.
- Keys:
  - dial down or MENU: the next document;
  - dial up or EXIT: the previous one;
  - pressing the dial: a full redraw.
- Home Assistant (push and poll) runs on this board through the integration player in
  **kobento-mcuApps** (`esp32/firmware/rcplayer`), which brings its own Wi-Fi, `ha.cpp` and
  the thermostat/IoT/wide documents. The minimal player in this library has no networking.
- Console extras: `epd` (refresh counts and times), `epd clear`, `keys` (five seconds of the
  key levels).
- No touch, battery gauge, RTC, motion sensor or sound.
