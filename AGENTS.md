# AGENTS.md: getting up to speed on kobento-mcu

This is for an agent (or person) picking up this repository cold. Read it before changing
anything. README.md has the family overview and the layout. `esp32/README.md` and
`stm32/README.md` have the per-platform details; each `esp32/boards/<board>/README.md` has
that board's hardware facts.

## What this is

**Kobento** is an implementation of the **RemoteCompose** (RC) spec defined by Android/androidx.
RemoteCompose is the spec and the `.rc` format; Kobento is the implementation brand (see
README.md). This repo, **kobento-mcu**, is the **microcontroller library for displaying RC
documents**, plus a minimal sample player for each supported board.

- The library is `components/`: the engine `rccore` (built from `../remotecompose-experiments`,
  the future kobento-cpp), **KobentoVG** (our ThorVG fork + its `ThorvgPaintContext` backend,
  `components/kobentovg`, namespace `kobentovg`), and `rcstage` (puts one RC document on a
  screen: player task, touch, device-state variables).
- A sample player embeds the documents in `documents/` and shows them, cycled by the board's
  button or the serial console. That is all it does.

**This repo does no networking and ships no integrations.** Delivering documents to a device
(formerly rcNet) belongs to **kobento-net**; apps and integrations (Home Assistant, Muse,
Wi-Fi UI) belong to **kobento-mcuApps**. Don't add Wi-Fi, HTTP, BLE, or product integrations
here — a fresh clone must build into a working display-only player.

The androidx Java implementation is the source of truth for RC. Conventions this repo touches
(host actions, system/device-state variables) are drafts in `docs/`, for the owner to review.

### Repository layout (paths in this guide are relative to the repo root)

Shared library at the top level (`components/`, `third_party/`, `documents/`, `assets/`,
`docs/`, `tools/`); the ESP32 sample under **`esp32/`** (`esp32/boards/`,
`esp32/firmware/rcplayer`, `esp32/host/`, `esp32/cmake/`) and the STM32 sample under
**`stm32/`**.

## Sibling repositories (expected next to this one)

| Path | What | Notes |
|---|---|---|
| `../remotecompose-experiments` | `players/cpp/lib/rccore`, the engine (future kobento-cpp) | Built through `components/rccore`. **Owned by other agents:** report engine bugs (below), don't change it from here. |
| `../STM32CubeU5` | ST's STM32CubeU5 HAL + BSP | Only for the `stm32/` build. |

## Toolchain (macOS)

- **ESP-IDF v6.0.1** at `~/esp/esp-idf-v6`. Run `. ~/esp/esp-idf-v6/export.sh` in the same
  shell command as `idf.py`, because shell state doesn't persist between tool calls.
- **Arm GNU Toolchain 15.2** for STM32, unpacked at
  `~/tools/arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi` (the cmake toolchain file
  points there; override with `-DARM_GCC_BIN=...`).
- **probe-rs** (`brew install probe-rs-tools`) flashes and screenshots the STM32.
- The host renderer builds with plain CMake + Ninja (see below).
- The shell is zsh: quote arrays as `"${arr[@]}"`, and use `$=var` for word splitting.

## Build, flash, test — ESP32

```sh
. ~/esp/esp-idf-v6/export.sh
cd esp32/firmware/rcplayer
idf.py -B build/<board> -DBOARD=<board> build                 # <board> = a dir under esp32/boards/
idf.py -B build/<board> -DBOARD=<board> -p <PORT> app-flash   # app only: keeps NVS
```

- `esp32/cmake/board.cmake` selects the board (`-DBOARD=`, default `m5stack_tab5`). Each board
  gets its own build directory and sdkconfig. **After changing any `sdkconfig.defaults`, delete
  `build/<board>/sdkconfig`,** or the old values stay.
- Ports move; list them with `ls /dev/cu.usbmodem* /dev/cu.usbserial*`.
- **Talk to a running board** with `tools/snap.py` and `tools/peek.py` (raw `os.open` + termios,
  so no DTR/RTS reset): `tools/snap.py out.png` screenshots, `tools/snap.py --cmd "doc 3"` sends
  a console line. Console: `list`, `doc N`, `next`, `prev`, `snap`, `stats`, `bench`,
  `res full|half`, `act NAME[=VALUE]`, `named [PREFIX]`, plus each board's own diagnostics.

## Build, flash, test — STM32U5A9J-DK

Run from `stm32/` (see `stm32/README.md`). Needs `../STM32CubeU5`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
ninja -C build
probe-rs download --chip STM32U5A9NJ --connect-under-reset build/rcplayer.elf
probe-rs reset --chip STM32U5A9NJ
tools/term.py --cmd "doc 3" --seconds 3     # console over the ST-LINK VCP (115200)
tools/snap.py shot.png                       # screenshot, read over the debugger (probe-rs)
```

`st-flash` fails verification on this chip (STM32U5 TrustZone/flash) — use **probe-rs with
`--connect-under-reset`**. Console: `list`, `doc N`, `next`, `prev`, `stats`, `time`.

## Host renderer and conformance (macOS/Linux)

The same engine + ThorVG backend the boards run, for fast iteration and pixel comparison.

```sh
cmake -S esp32/host -B esp32/host/build -G Ninja && ninja -C esp32/host/build
esp32/host/build/rc2png doc.rc out.png --fit W H --density D   # --named NAME=VAL, --sys ID=VAL
esp32/host/build/rc2png --click X,Y ...                        # run a tap, print host actions
tools/sweep_skia.sh DIR OUT 400   # same engine, Skia vs ThorVG; isolates backend bugs
```

`rc2json` (built in `../remotecompose-experiments`) decodes a `.rc` — the engine-independent
check when a document looks wrong.

## Documents and tooling

- `documents/*.rc` are embedded by globbing. `documents/fit/*.rc` are designed at a fixed size
  and scaled whole to the screen (`rcstage::load` fit). `documents/3d/*.rc` are embedded only
  with `CONFIG_KOBENTOVG_3D`. A board may pick an ordered subset with
  `CONFIG_RCPLAYER_DOCUMENTS` in its `sdkconfig.rcplayer.defaults`.
- `tools/logo/` turns an SVG into an RC document: `svg2rc.py` bakes SVG transforms into
  absolute paths (RC has no skew matrix) and emits `paint`+`drawPath`; `build_logos.sh`
  compiles the brand logos to `documents/`. `tools/splash/` holds the `json2rc` compiler
  (`compile.mjs`) it uses.
- Check a change on the Mac first with `rc2png` before flashing; when a refactor shouldn't
  change a document, confirm the `.rc` is byte-identical (`git diff --stat`).

## Architecture in one page

- **`board/board.h`** is the only path to hardware. Every board implements it: display init +
  present, touch, backlight, power, a console hook, and the constants
  `kWidth/kHeight/kDensity/kRound/kEink/kName/kDisplayName`. Documents lay out in dp = px /
  `kDensity`. Copy an existing board's header when adding one.
- **`components/rcstage`** puts one document on screen: a player task that polls touch
  (down/drag/up, onClick, a 1.5 s long press → `Hooks::longPress`), renders only when something
  is dirty or the engine's repaint delay is due, and hands the ARGB frame to `board::presentAsync`.
  `device_state.cpp` samples power/memory/time once a second into system and named variables.
- **`esp32/firmware/rcplayer/main/main.cpp`** is the sample app: it embeds the documents
  (`embedded_docs.h`, generated by the CMakeLists), shows the first one, and steps through the
  rest on the button or console. It handles the generic host actions `system.backlight`,
  `doc.show/next/back`; a document declaring `Control.next`/`Control.back` receives the buttons
  as press counts. No networking.
- **`stm32/src/main.cpp`** is the bare-metal equivalent: a single-threaded loop that loads a
  document from flash and redraws when something changed or the schedule says so.

## Engine (rccore) — owned externally

Find engine bugs with `rc2png` first; it is the same engine as the device. Since 2026-10-19
the engine belongs to other agents: **write a bug up here rather than fixing it in
`../remotecompose-experiments`.**

Known gaps against the Java reference (androidx `remote-core`), not fixed:
- **Text styles ignored.** CoreText parameter 24 (`textStyleId`) is skipped, so a size/colour/
  weight from a `TextStyle` (opcode 242) is lost and size falls back to 36.
- **Repaint scheduling.** `TimeAttributeOp` calls `needsRepaint()` where Java calls `wakeIn`,
  so time documents animate continuously and never sleep; no case for `ID_TIME_IN_HR`;
  once a `wakeIn` delay is consumed it can only shrink.
- **`densityBehavior` (header tag 27) isn't read**; rcstage lays out in dp and scales drawing,
  so DP documents match Java and legacy/pixels documents draw larger.
- `timeInSec` differs between players (Java: second within the hour; rccore: seconds since
  midnight). `rc2json` stops on `documents/fit/maze.rc` (unknown opcode 27).
- `rccore` is built with `-DESP_PLATFORM=1` on the STM32 for its newlib time path (newlib's
  `struct tm` has no `tm_gmtoff`); report so the guard keys on newlib, not ESP_PLATFORM.

## The ThorVG backend (KobentoVG) vs the Skia reference

The boards draw with the ThorVG fork; the desktop reference is Skia (`rc2image` in
`../remotecompose-experiments`). RcVG parity work added sweep/conic gradients, text-on-path and
2D meshes (see `docs/RCVG_STUDY.md`). Still missing or approximate: runtime (AGSL) and bitmap
shaders, Porter-Duff blend modes for drawing (SrcOver only), inverse fills, path combining,
text-on-a-path gradients, fonts beyond Roboto regular/bold/mono (no fallback: CJK/emoji draw
nothing), WebP/GIF. When changing the backend, render every document before and after
(`rc2png --time` for a fixed clock) and diff the pixels.

## Board-specific traps (each board's README has more)

- **Waveshare round (CH343P bridge) on macOS:** the built-in CDC driver corrupts packets of
  ~512 B+, so `idf.py flash` fails — flash with `tools/esptool256.py --no-stub` (256-byte
  blocks). Only 2 MB PSRAM.
- **ESP32 / ESP32-S3 with the full engine:** set `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0` and
  `RESERVE_INTERNAL=65536`, or the engine's thousands of small allocations fill internal RAM.
- **PaperS3 (e-ink):** the driver is ours (M5GFX timing ported into `board.cpp`). Refreshes
  only changed pixels (~0.5 s). Console is USB-Serial/JTAG (GPIO44 is power-off). Animated
  documents look jumpy.
- **Tab5 (ESP32-P4 rev v1.x):** IDF v6 needs `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`. PSRAM
  bandwidth sets the frame rate.
- **Core2:** the classic ESP32; AXP192 or AXP2101 detected at run time; no 3D.
- **AMOLED (Waveshare S3-Touch-AMOLED-1.8):** 368x448, 3D enabled.
- **STM32U5:** see `stm32/README.md` (GFXMMU, PSRAM MPU region, single-threaded runtime).

## Writing style used in this repo

- Comments explain *why*, at about the density of the surrounding code.
- Docs use short declarative sentences, units on every number, and tables for comparisons.
- Board READMEs record sources and the date they were read, and mark what wasn't verified.
- **Commit or push only when the owner asks.** End commit messages with the `Co-Authored-By`
  line your harness specifies (or none, per the owner's rule).
