# Kobento

**UI composition, anywhere.**

Kobento is an implementation of the [RemoteCompose](https://developer.android.com/jetpack/androidx/releases/compose-remote)
spec defined by Android/androidx. RemoteCompose is the format and the spec; **Kobento is the
implementation brand** — a family of players and tools that render RemoteCompose (`.rc`)
documents on hardware the reference implementation does not reach.

This repository, **`kobento-mcu`**, is the **microcontroller library for displaying RC
documents**: the engine, the renderer, the per-screen player, and a minimal sample app for
each supported board that embeds documents and shows them. Download it, build it for a board,
and you have a working RC player. It does no networking and ships no integrations — those are
separate repositories.

| Repo | What |
|---|---|
| **kobento-mcu** (this) | the MCU library for displaying RC docs + minimal sample players (ESP32, STM32) |
| kobento-net *(planned)* | networking/transport — pushing documents to a device (formerly rcNet) |
| kobento-mcuApps *(planned)* | full apps and integrations built on kobento-mcu (Home Assistant, Muse, Wi-Fi UI) |
| kobento-cpp *(planned)* | the C++ RemoteCompose core engine (today built from `../remotecompose-experiments`) |
| kobento-typescript / kobento-web / kobento-inspector *(planned)* | TS player + `json2rc`, web player, document inspector |

## Layout

```
components/          the library, shared by every target
  rccore/            CMake glue: builds the engine from ../remotecompose-experiments (→ kobento-cpp)
  thorvg/            CMake glue + config.h for the vendored ThorVG fork (host and firmware)
  kobentovg/         ThorvgPaintContext: the RemoteCompose PaintContext on ThorVG's rasterizer
  rcstage/           one RC document live on a screen: player task, touch, variables, device state
third_party/thorvg/  the vendored ThorVG fork (branded KobentoVG). See PROVENANCE.md
documents/           sample .rc documents the players embed. See PROVENANCE.md
assets/fonts/        Roboto and Roboto Mono (Apache 2.0)
docs/                RC conventions (system variables, action patterns, the RcVG study)
tools/               the json2rc compiler, the logo generator, device scripts, conformance sweeps
brand/               Kobento brand identity + logo assets. See BRAND.md
esp32/               the ESP32 sample: boards/, firmware/rcplayer, host/, cmake/
stm32/               the STM32U5 sample: board/, src/, tools/
linux/               the Linux player (Raspberry Pi, desktop): framebuffer + console, same engine
```

## Supported platforms

Each is a self-contained sample that embeds the documents in `documents/` and displays them,
cycled by the board's button or the serial console.

| Platform | Build + flash | Notes |
|---|---|---|
| ESP32 boards (Tab5, Core2, PaperS3, CrowPanel, AMOLED, round) | [esp32/README.md](esp32/README.md) | ESP-IDF; `-DBOARD=<board>` |
| STM32U5A9J-DK | [stm32/README.md](stm32/README.md) | bare-metal; probe-rs to flash |
| Linux (Raspberry Pi, incl. Zero; desktop) | [linux/README.md](linux/README.md) | full Linux, not an MCU; CMake, framebuffer `/dev/fb0`, no ESP-IDF |

Host renderer (same engine + ThorVG backend, for fast iteration and pixel comparison):
`cmake -S esp32/host -B esp32/host/build -G Ninja && ninja -C esp32/host/build`, then
`esp32/host/build/rc2png doc.rc out.png --fit W H`.

New here, or an agent picking this up? [AGENTS.md](AGENTS.md) has the setup, per-platform
build/flash/test, the architecture and the known traps.

## Expected beside this repository

- `../remotecompose-experiments` — the `rccore` engine (the future kobento-cpp). Built in place
  for now; the build repoints to the Kobento libs once they are rebuilt.
- `../STM32CubeU5` — ST's HAL/BSP, for the STM32 build only.

## Relationship to RemoteCompose

Kobento is a clean-room *implementation brand*, not a fork of the name. The on-the-wire format,
the opcodes and the `.rc` extension remain **RemoteCompose** — the spec Kobento conforms to.
Where a document, an engine, or a tool is derived from upstream RemoteCompose or Android
sources, [PROVENANCE.md](PROVENANCE.md) records it.
