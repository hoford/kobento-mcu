# Kobento on ESP32 (`kobento-mcu/esp32`)

The ESP32 sample for **Kobento** — a minimal RemoteCompose player: it embeds the documents in
`../documents/` and shows them on the board's screen, stepped through with the board's button
or the serial console. One tree, many boards; the engine, renderer and player are shared and
each board supplies its hardware through one interface (`board/board.h`). No networking (that
is kobento-net / kobento-mcuApps).

> **Paths** below are relative to `esp32/` — run the commands from here. Shared code at the
> repo root (`../components/`, `../documents/`, `../tools/`, …) is reached with `../`.

| Board | Chip | Screen | State |
|---|---|---|---|
| [M5Stack Tab5](boards/m5stack_tab5/README.md) | ESP32-P4 rev v1.3, 32 MB PSRAM | 720x1280 MIPI-DSI, touch | ✅ |
| [M5Stack Core2](boards/m5stack_core2/README.md) | ESP32, 8 MB PSRAM | 320x240 SPI ILI9342C, touch | ✅ (static documents; animation is slow) |
| [Waveshare ESP32-S3 1.28" round](boards/waveshare_s3_lcd128/README.md) | ESP32-S3R2, 2 MB PSRAM | 240x240 round GC9A01A | ✅ flash with `../tools/esptool256.py` |
| [M5Stack PaperS3](boards/m5stack_papers3/README.md) | ESP32-S3R8, 8 MB PSRAM | 540x960 4.7" e-ink | ✅ (~0.5 s per refresh) |
| [Elecrow CrowPanel 5.79"](boards/elecrow_crowpanel579/README.md) | ESP32-S3-WROOM-1-N8R8, 8 MB PSRAM | 792x272 e-paper, 5 keys | ✅ (~1.9 s per full refresh) |
| [Waveshare ESP32-S3-Touch-AMOLED-1.8](boards/waveshare_s3_amoled18/README.md) | ESP32-S3R8, 8 MB PSRAM | 368x448 AMOLED, touch | ✅ |

## Build, flash, run

Needs ESP-IDF v6.0.1 installed (anywhere — `~/esp/esp-idf-v6` below is just where this repo's
author put it; use your own path).

```sh
. ~/esp/esp-idf-v6/export.sh                                  # or wherever you installed ESP-IDF
cd firmware/rcplayer
idf.py -B build/<board> -DBOARD=<board> build                 # <board> = a dir under boards/
idf.py -B build/<board> -DBOARD=<board> -p <PORT> app-flash   # app only; keeps saved NVS
```

- `cmake/board.cmake` selects the board (`-DBOARD=`, default `m5stack_tab5`). Each board gets
  its own build directory and sdkconfig. **After changing a `sdkconfig.defaults`, delete
  `build/<board>/sdkconfig`.**
- By default every document in `../documents/` (and `../documents/fit/`, and `../documents/3d/`
  when `CONFIG_KOBENTOVG_3D` is on) is embedded. A board can pick an ordered subset with
  `CONFIG_RCPLAYER_DOCUMENTS` in `boards/<board>/sdkconfig.rcplayer.defaults`.
- Ports move; list them with `ls /dev/cu.usbmodem* /dev/cu.usbserial*`.

## Talking to the device (no reset, no TTY needed)

`../tools/snap.py` and `../tools/peek.py` open the port with raw `os.open` + termios, so they
don't reset the chip.

```sh
../tools/snap.py out.png                 # screenshot of what is on the panel
../tools/snap.py --cmd list              # embedded documents
../tools/snap.py --cmd "doc 3"           # open document 3. Also: next, prev
../tools/snap.py --cmd "res half"        # render at half res and let the PPA upscale 2x
../tools/snap.py --cmd stats             # frame timing
../tools/peek.py /dev/cu.usbmodemXXXX 10 # passively read the log
```

Console commands: `list`, `doc N`, `next`, `prev`, `snap`, `stats`, `bench`, `res full|half`,
`act NAME[=VALUE]` (run a host action), `named [PREFIX]` (the showing document's variables),
plus each board's own diagnostics. A 1.5 s long-press on the screen also steps to the next
document.

## Host renderer and conformance

The same engine + ThorVG backend, on the Mac, for fast iteration and pixel comparison.

```sh
cmake -S host -B host/build -G Ninja && ninja -C host/build
host/build/rc2png doc.rc out.png --fit 360 640          # player mode: lays out to the surface
../tools/sweep_skia.sh DIR OUT 400   # same engine, Skia vs ThorVG; isolates backend bugs
```

## Adding a board

1. Create `boards/<board>/` with `sdkconfig.defaults`, which must set `CONFIG_IDF_TARGET`.
2. Add `components/board`, implementing `board/board.h`. Copy the Tab5's header and keep every
   function, using no-ops where the board lacks the hardware. Put the board's BSP beside it.
3. Build with `-DBOARD=<board>`.

## Learnings

**Platform**

- **PSRAM bandwidth is the budget.** A `memset` of the 720x1280 ARGB buffer takes ~25.6 ms
  (~144 MB/s); every full-screen raster pass costs about that, a blended pass ~2.6x. Half-res
  render + PPA upscale reaches ~34 fps on the Tab5.
- **ESP32-P4 rev v1.x on IDF v6** needs `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`.
- **The console must be USB-Serial-JTAG** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`) on boards that
  use it; the default UART0 would make the USB port output-only.
- **Opening the port with pyserial resets the P4** (DTR/RTS). Raw `os.open` + termios avoids it.

**Rendering**

- **ThorVG `Text::size()` is in points at 96 DPI**, so pixel sizes are multiplied by 0.75.
- **Gradient text** is drawn as a gradient rectangle masked by the text (fill in font units).
- **No font fallback yet** — glyphs Roboto lacks draw nothing. A symbols fallback face would fix it.
- See `../docs/RCVG_STUDY.md` for the ThorVG-vs-Skia parity work and the remaining gaps.

**Libraries**

- **esp_lvgl_port 2.x doesn't build on IDF v6.0.1,** so the BSP is vendored without LVGL.
