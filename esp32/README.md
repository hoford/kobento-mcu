# Kobento on ESP32 (`kobento-mcu/esp32`)

The ESP32 side of **Kobento** — RemoteCompose players and [Muse Gadget SDK](../../muse-gadget-sdk)
firmware for ESP32 devices of all sizes. One tree, many boards: the engine, the player and
the apps are shared, and each board under `boards/` supplies its hardware through one
interface (`board/board.h`).

> **Paths in this file** are relative to `esp32/` (run shell commands from here). Local dirs
> — `boards/`, `firmware/`, `host/`, `cmake/` — are bare; **shared** code at the repo root —
> `components/`, `third_party/`, `documents/`, `assets/`, `docs/`, `tools/` — is reached with
> `../`. The top-level [README](../README.md) shows the same commands from the repo root.

New here, or an agent picking this up? Start with [AGENTS.md](../AGENTS.md): setup, the rules
for each device, the architecture and the known traps.

| Board | Chip | Screen | Radio | State |
|---|---|---|---|---|
| [M5Stack Tab5](boards/m5stack_tab5/README.md) | ESP32-P4 rev v1.3, 32 MB PSRAM | 720x1280 MIPI-DSI, touch | ESP32-C6 over SDIO | ✅ rcplayer, muse-gadget |
| [M5Stack Core2](boards/m5stack_core2/README.md) | ESP32 (D0WDQ6-V3), 8 MB PSRAM | 320x240 SPI ILI9342C, touch | on-chip Wi-Fi/BLE | ✅ rcplayer (static documents; animation is slow) |
| [Waveshare ESP32-S3 1.28" round](boards/waveshare_s3_lcd128/README.md) | ESP32-S3R2, 2 MB PSRAM | 240x240 round SPI GC9A01A, touch on the Touch variant | on-chip Wi-Fi/BLE | ✅ rcplayer (static documents); flash with tools/esptool256.py |
| [M5Stack PaperS3](boards/m5stack_papers3/README.md) | ESP32-S3R8, 8 MB PSRAM | 540x960 4.7" e-ink (ED047TC1, parallel), GT911 touch | on-chip Wi-Fi/BLE | ✅ rcplayer (static documents; about 0.5 s per refresh) |
| [Elecrow CrowPanel 5.79"](boards/elecrow_crowpanel579/README.md) | ESP32-S3-WROOM-1-N8R8, 8 MB PSRAM | 792x272 e-paper (2x SSD1683, SPI), five keys, no touch | on-chip Wi-Fi/BLE | ✅ rcplayer (three static documents; about 1.9 s per full refresh) |
| [Waveshare ESP32-S3-Touch-AMOLED-1.8](boards/waveshare_s3_amoled18/README.md) | ESP32-S3R8, 8 MB PSRAM | 368x448 1.8" AMOLED (SH8601 / CO5300, QSPI), FT3168 / CST816 touch, AXP2101 | on-chip Wi-Fi/BLE | ✅ rcplayer |

## Status (M5Stack Tab5)

| Step | State |
|---|---|
| 1. RemoteCompose player on the Tab5 | ✅ Upstream `rccore` with a ThorVG renderer. Touch works. 9 demo documents are embedded. |
| Wi-Fi via the C6 (esp_hosted 3.x over SDIO) | ✅ Joins the dev network; credentials live in NVS (survive reflash). |
| rcNet receiver (push `.rc` from a Mac or phone) | ✅ Verified with Origami2's `send.py`; mDNS `tab5.local` / `_rcnet._tcp`. |
| BLE via the C6 (NimBLE + Hosted-HCI) | ✅ Scan and advertise beside Wi-Fi (`ble` console command). |
| 2. Muse gadget support | ✅ `firmware/muse-gadget` pairs with the Muse app as `MuseGadget-Tab5-XXXXXX`; Muse pushes RC documents to it. |
| 3. Muse ↔ RemoteCompose integration | 🟡 Device state as RC system/named variables, host actions, live start-up screen ([proposal](../docs/RC_SYSTEM_VARIABLES.md)). |

## Layout

This tree (`esp32/`):

```
boards/<board>/      one directory per board
  components/board/  the board interface (board/board.h): display, touch, backlight, radio,
                     power, battery; the shared code's only way to the hardware
  components/...     the board's BSP and drivers
  sdkconfig.defaults chip, memory, console, radio wiring
  sdkconfig.<app>.defaults   board settings one app needs (e.g. the Muse BLE name suffix)
  README.md          hardware notes
firmware/rcplayer/   RC player app: embedded documents, serial console, Wi-Fi + rcNet
firmware/muse-gadget/ the Muse Gadget SDK's app with an RC screen (components/muse_rc)
cmake/board.cmake    board selection (-DBOARD=...), shared by the apps
host/                macOS/Linux build of the same renderer: rc2png, plus the Skia reference build
```

Shared across targets, at the repo root (reached as `../`):

```
../components/rccore      CMake glue: builds rccore from the sibling remotecompose-experiments
../components/thorvg      CMake glue + config.h for the vendored ThorVG fork (host and ESP-IDF)
../components/kobentovg   ThorvgPaintContext: the RC PaintContext on ThorVG's rasterizer (KobentoVG)
../components/rcstage     one RC document live on a screen: player task, touch, variables, actions
../documents/            the .rc documents the apps embed (muse_splash.rc is the start-up screen)
../third_party/thorvg    the ThorVG fork, with local patches (see ../PROVENANCE.md)
../tools/                snap.py, peek.py, update-muse.sh, splash generator, compare/sweep scripts
../docs/                 RC conventions under review (system variables, action patterns)
../assets/fonts/         Roboto and Roboto Mono (Apache 2.0)
```

Expected beside the repository: `remotecompose-experiments` (rccore) and, for the Muse app,
`muse-gadget-sdk` on branch `tab5-p4-hosted`.

## Adding a board

1. Create `boards/<board>/` with `sdkconfig.defaults`, which must set `CONFIG_IDF_TARGET`.
2. Add `components/board`, implementing `board/board.h`. Copy the Tab5's header and keep
   every function, using no-ops where the board lacks the hardware. Put the board's BSP or
   drivers beside it.
3. Build with `-DBOARD=<board>`. Each board gets its own build directory and sdkconfig.

rccore's newlib guard (`tm_gmtoff`), host actions and the proposed ids 38–41 are on
remotecompose-experiments `main`.

## Build and flash

> **A Tab5 paired with Muse must not get this firmware.** rcplayer's app starts at 0x10000 and
> overwrites the Muse NVS at 0x11000, which holds the pairing. Use `../tools/update-muse.sh` there.

```sh
. ~/esp/esp-idf-v6/export.sh
cd firmware/rcplayer
idf.py -B build/m5stack_tab5 -DBOARD=m5stack_tab5 build
idf.py -B build/m5stack_tab5 -DBOARD=m5stack_tab5 -p /dev/cu.usbmodem31101 flash
```

To skip the serial `wifi` command, put Wi-Fi credentials in a gitignored
`firmware/rcplayer/sdkconfig.secrets`:

```
CONFIG_RCPLAYER_WIFI_SSID="..."
CONFIG_RCPLAYER_WIFI_PASSWORD="..."
```

## Talking to the device (no reset, no TTY needed)

```sh
../tools/snap.py out.png                 # screenshot of what is on the panel (360x640)
../tools/snap.py --cmd list              # embedded documents
../tools/snap.py --cmd "doc 3"           # Flappy Droid. Also: next, prev
../tools/snap.py --cmd "res half"        # render at 360x640 and let the PPA upscale (about 34 fps)
../tools/snap.py --cmd "wifi SSID PASS"  # store Wi-Fi credentials in NVS and connect
../tools/snap.py --cmd net               # Wi-Fi and rcNet status
../tools/snap.py --cmd scan              # list visible APs
../tools/snap.py --cmd bench             # raw raster throughput
../tools/peek.py /dev/cu.usbmodem31101 10   # passively read the log
```

On the device itself, a 1.5 s long-press switches to the next document.

Once Wi-Fi is up, you can push documents with Origami2's rcNet sender
(`rcNet/python/send.py`, iOS or Android). You can also post them directly:
`curl -H "X-Filename: x.rc" --data-binary @x.rc http://tab5.local:8937/send`

## Muse build (firmware/muse-gadget)

Built from `../muse-gadget-sdk/esp32` (branch `tab5-p4-hosted`) through its
`cmake/project.cmake`. The SDK's `main` is the app. The board and the RC screen come in
through the new `gadget_ext.h` hooks (`components/muse_rc`):

- **Early init:** powers the board's radio and starts rcstage.
- **Status:** the pairing state appears as a banner over the RC document.
- **Pairing:** a tap stands in for the setup button.
- **Commands:** `rc.list`, `rc.show`, `rc.show_url`, `rc.set` and `rc.banner` are registered
  with Muse.

```sh
# Token: CONFIG_GADGET_SDK_TOKEN="mgst_..." in firmware/muse-gadget/sdkconfig.secrets (gitignored)
../tools/update-muse.sh [--board B]            # build + write the app only; pairing and Wi-Fi survive
../tools/snap.py --prefix '>' out.png          # screenshot; the SDK console takes '>' lines
../tools/snap.py --prefix '>' --cmd tap        # same as tapping the screen (confirms pairing)
../tools/snap.py --prefix '>' --cmd "show flappy"   # also: set NAME VALUE, banner TEXT, vars, stats
../tools/snap.py --prefix '>' --cmd sys        # device state; Tab5 also: "sys history", expander
```

`../tools/update-muse.sh` writes only the app partition (0x20000), so the pairing, device
credentials and BLE bonds in NVS survive every update. Avoid `idf.py erase-flash`; it wipes
them, and the device must be paired again. A full `idf.py flash` is also safe: it doesn't
touch NVS.

The start-up screen is `muse_splash.rc`. `python3 ../tools/splash/make_splash.py` builds it
from the traced Muse mark and `../tools/splash/crane.svg` (Wikimedia's "Origami - Crane"). It is
compiled with `json2rc.js` from [RemoteCompose-Examples](https://github.com/hoford/RemoteCompose-Examples),
vendored in that directory.

SDK changes on the branch, all generic:

| Change | Files |
|---|---|
| esp_hosted radio support for the P4 | `hosted_radio.c` |
| Extension hooks and their weak defaults | `gadget_ext.h`, `gadget_ext.c` |
| An `EXTERNAL` status backend | `led_status.c` |
| A `HOMEHUB_BUTTON` option for boards with no free button | `app.c` |
| Identity from the eFuse MAC when the chip has no radio | `identity.c` |

## Host renderer and conformance

```sh
cmake -S host -B host/build -G Ninja && ninja -C host/build
host/build/rc2png doc.rc out.png --fit 360 640          # player mode: lays out to the surface
../tools/sweep_skia.sh DIR OUT 400   # same engine, Skia vs ThorVG; isolates backend bugs
../tools/sweep.sh DIR OUT 400        # vs the TS player; mixes in engine differences
```

## Learnings

**Platform**

- **PSRAM bandwidth is the budget.** A `memset` of the 720x1280 ARGB buffer takes 25.6 ms
  (about 144 MB/s). Every full-screen raster pass costs about that much, and a blended pass
  costs about 2.6 times as much. Resulting frame rates for animated docs:

  | Change | Frame rate |
  |---|---|
  | Skip the redundant clear pass | 5 → 7 fps |
  | Double-buffer and overlap the PPA conversion with the next render | 9–11 fps |
  | Render at half resolution and upscale with the PPA | about 34 fps |

  The next step is banded rendering in internal SRAM.
- **ESP32-P4 rev v1.x on IDF v6** needs `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`.
- **The console must be USB-Serial-JTAG** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`). The default
  is UART0, and then the USB port only carries output.
- **Opening the port with pyserial resets the P4.** Lowering DTR before RTS briefly drives
  RTS=1 with DTR=0, which the chip treats as a reset. Raw `os.open` plus termios avoids this.

**Rendering**

- **ThorVG v1.0.7 text bug:** calling `Text::metrics()` on a space poisons the font, and any
  later text containing a space draws nothing. This is patched in our vendored copy.
- **ThorVG `Text::size()` is in points at 96 DPI**, so pixel sizes are multiplied by 0.75.
- **ThorVG applies a text fill in font units**, so gradient text is drawn as a gradient
  rectangle masked by the text.
- **`rc2image` (Skia) clears once and then paints `RC_FRAMES` times,** so translucent layers
  accumulate. Compare with a single frame.
- **No font fallback yet.** Glyphs Roboto lacks (e.g. `⬩` U+2B29 in hydration_wave) draw
  nothing; Skia on macOS falls back to system fonts. A symbols fallback face would fix it.
- **The C++ engine renders themed documents inverted** compared with the TS player. This is
  an rccore issue, not a renderer one.

**Libraries**

- **esp_lvgl_port 2.x does not build on IDF v6.0.1,** so the BSP is vendored without LVGL.
- **esp_hosted 3.0.9 works with the Tab5's factory C6 firmware** (reported as 0.0.0, with a
  "major version mismatch" warning). Wi-Fi works, and the C6 advertises HCI over SDIO for BLE.
