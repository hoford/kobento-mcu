# Kobento on the STM32U5A9J-DK

The STM32 side of **Kobento** — a bare-metal RemoteCompose player for ST's STM32U5A9J-DK
discovery kit (STM32U5A9NJ, Cortex-M33 at 160 MHz, a 480 x 480 round MIPI-DSI panel, 64 MB
HSPI PSRAM). It builds the repo's shared components (`../components/{rccore,thorvg,kobentovg}`,
the fonts and the documents) unchanged and adds the board layer and a single-threaded player
loop. It is a pure display sample: the board has no radio, so there is no networking.

All embedded documents load and render on the board. Run every command below from this
`stm32/` directory.

## Sibling checkouts (beside the repo)

| Path | What |
|---|---|
| `../remotecompose-experiments` | the engine, `players/cpp/lib/rccore` (owned by other agents: report bugs, don't edit) |
| `../STM32CubeU5` | ST's HAL, CMSIS and this board's BSP (`git clone --recurse-submodules https://github.com/STMicroelectronics/STM32CubeU5`) |

## Tools

- **Compiler:** Arm GNU Toolchain 15.2 (any recent release works). The author unpacked it in
  `~/tools/arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi`, which is the toolchain
  file's default; install yours anywhere and point at it with `-DARM_GCC_BIN=.../bin`,
  `$ARM_GCC_BIN`, or `arm-none-eabi-gcc` on PATH. The Homebrew cask needs sudo; the tarball
  from developer.arm.com doesn't.
- **Flashing:** probe-rs (`brew install probe-rs-tools`), about 50 s for the 1.9 MB image.
  - `st-flash` (stlink 1.9) also works, but takes about 4 minutes.
  - OpenOCD 0.12 doesn't know this chip (device id 0x481).

## Build, flash, talk

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
ninja -C build
probe-rs download --chip STM32U5A9NJ --connect-under-reset build/rcplayer.elf
probe-rs reset --chip STM32U5A9NJ
tools/term.py --seconds 10                 # the log (ST-LINK virtual COM port, 115200)
tools/term.py --cmd "doc 3" --seconds 3    # a console command
tools/snap.py shot.png                     # a screenshot, read over the debugger
```

- **Console:** `list`, `doc N`, `next`, `prev`, `stats`, `time UNIX_SECONDS`, `hold`, `run`.
- **Going to the next document:** long-press the screen (1.5 s) or press the blue USER
  button.
- **The clock** starts at the build time, US Pacific; `time` sets it.

**The factory demo** is backed up in `~/backups/stm32u5a9j-dk/`, which has a README with
the command to restore it.

## How it fits together

- **`board/board.c`** is the hardware layer:
  - clocks, and the instruction and data caches;
  - the log port, with an interrupt-fed receive ring;
  - the panel, through ST's BSP: DSI, LTDC, and the GFXMMU, which keeps only the visible
    circle in RAM;
  - touch, through a Sitronix controller;
  - the USER button;
  - PSRAM, the faults report, and `_sbrk`.
- **`src/main.cpp`** is the player:
  - it loads a document straight from flash, with no copy;
  - it forwards touch;
  - it draws a frame only when something changed or the engine's schedule
    (`getRepaintDelay`) says one is due.
- **Rendering:**
  - ThorVG draws into `sFrame`, a 480 x 480 ARGB8888 buffer in on-chip SRAM.
  - The DMA2D then copies it into the GFXMMU's virtual buffer, whose lines are 768 pixels
    apart. The copy takes about 5 ms.
- **Memory:**
  - The on-chip SRAM holds the panel's buffer (737 KB) and `sFrame` (921 KB).
  - The heap is in PSRAM, behind an MPU region and the data cache. By default
    `0xA0000000` is device memory: uncached, and unaligned accesses fault.
  - A large document needs the PSRAM: the start screen takes 380 KB once parsed.
- **`src/runtime.c` and `src/single_thread_mutex.h`** fill in what the bare-metal toolchain
  lacks:
  - 64-bit atomics;
  - thread-local storage (`__aeabi_read_tp` must be assembly, because the ABI lets it change
    r0 only);
  - a no-op `std::mutex`, since the toolchain's libstdc++ is single-threaded.
- **`rccore` is built with `-DESP_PLATFORM=1`,** for its newlib time path: newlib's
  `struct tm` has no `tm_gmtoff`. This should be reported to the engine owners, so that the
  guard keys on newlib.

## Speed (first build)

| Document | Engine | ThorVG | Copy |
|---|---|---|---|
| `server_clock` | 1 ms | 20 ms | 5 ms |
| `balls_animation_example` | 5 ms | 39 ms | 5 ms |
| start screen | 41 ms | 154 ms | 5 ms |
| `under_the_sea` | 27 ms | 183 ms | 5 ms |

ThorVG's raster is the cost. Next steps:
- Move ThorVG's working memory, which is now in PSRAM, into the free on-chip SRAM (about
  700 KB).
- Render the dirty region only.
- Look at the NeoChrom GPU2D, if its programming model is open.

## Licences

Four configuration files in `board/` are ST's, under BSD-3-Clause, and two come from CMSIS
Device, under Apache-2.0: see [board/LICENSE.md](board/LICENSE.md).
