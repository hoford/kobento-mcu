# Kobento

**UI composition, anywhere.**

Kobento is an implementation of the [RemoteCompose](https://developer.android.com/jetpack/androidx/releases/compose-remote)
spec defined by Android/androidx. RemoteCompose is the format and the spec; **Kobento is the
implementation brand** — a family of players and tools that render RemoteCompose (`.rc`)
documents on hardware the reference implementation does not reach.

This repository, **`kobento-mcu`**, is the microcontroller side of that family: one tree, two
MCU families, shared engine and renderer.

| Repo | What |
|---|---|
| **kobento-mcu** (this) | RemoteCompose players for microcontrollers: ESP32 and STM32 |
| kobento-cpp *(planned)* | the C++ RemoteCompose core engine (today built from `../remotecompose-experiments`) |
| kobento-typescript *(planned)* | the TypeScript player and `json2rc` compiler |
| kobento-web *(planned)* | the web player |
| kobento-inspector *(planned)* | document inspector / debugger |

## Layout

```
components/          shared by every target
  rccore/            CMake glue: builds the engine from ../remotecompose-experiments (→ kobento-cpp)
  thorvg/            CMake glue + config.h for the vendored ThorVG fork (host and firmware)
  kobentovg/         ThorvgPaintContext: the RemoteCompose PaintContext on ThorVG's rasterizer
  rcstage/           one RC document live on a screen: player task, touch, variables, actions
third_party/thorvg/  the vendored ThorVG fork (branded KobentoVG). See PROVENANCE.md
documents/           prebuilt .rc documents both targets embed. See PROVENANCE.md
assets/fonts/        Roboto and Roboto Mono (Apache 2.0)
docs/                RC conventions under review (system variables, action patterns, the RcVG study)
tools/               document generators (splash, dashboard), device scripts, conformance sweeps
brand/               Kobento brand identity. See BRAND.md
esp32/               the ESP32 side: boards/, firmware/ (rcplayer, muse-gadget), host/, cmake/
stm32/               the STM32U5 side: board/, src/, tools/
```

## Building

- **ESP32** — see [esp32/README.md](esp32/README.md). Host renderer:
  `cmake -S esp32/host -B esp32/host/build -G Ninja && ninja -C esp32/host/build`.
- **STM32U5** — see [stm32/README.md](stm32/README.md).

New here, or an agent picking this up? [AGENTS.md](AGENTS.md) has the setup, the per-device
rules, the architecture and the known traps.

## Expected beside this repository

- `../remotecompose-experiments` — the `rccore` engine (the future kobento-cpp). Built in place
  for now; the build repoints to the Kobento libs once they are rebuilt.
- `../muse-gadget-sdk` (branch `tab5-p4-hosted`) — for the ESP32 Muse Gadget app.
- `../STM32CubeU5` — ST's HAL/BSP, for the STM32 build.

## Relationship to RemoteCompose

Kobento is a clean-room *implementation brand*, not a fork of the name. The on-the-wire format,
the opcodes and the `.rc` extension remain **RemoteCompose** — the spec Kobento conforms to.
Where a document, an engine, or a tool is derived from upstream RemoteCompose or Android
sources, [PROVENANCE.md](PROVENANCE.md) records it.
