# Third-party notices and credits

Kobento's own code in this repository is under the Apache License 2.0 ([LICENSE](LICENSE)). The
software below belongs to its authors, keeps its own licence, and is gratefully credited here.
[PROVENANCE.md](PROVENANCE.md) explains how each piece fits in.

## In this repository

| Software | Where | Authors | Licence | Text |
|---|---|---|---|---|
| [ThorVG](https://github.com/thorvg/thorvg) v1.0.7, modified | `third_party/thorvg` | ThorVG Project | MIT | `third_party/thorvg/LICENSE` |
| LodePNG, inside ThorVG | `src/loaders/png` | Lode Vandevenne | zlib | file header |
| jpgd, inside ThorVG | `src/loaders/jpg` | Richard Geldreich | Public domain | file header |
| FreeType's rasterizer, adapted inside ThorVG | `src/renderer/cpu_engine/tvgSwRle.cpp` | The FreeType Project | FreeType License | file header |
| [Roboto](https://github.com/googlefonts/roboto-2) v2.138 | `assets/fonts/Roboto-*.ttf` | Google Inc., Christian Robertson | Apache-2.0 | [LICENSE](LICENSE) |
| [Roboto Mono](https://github.com/googlefonts/robotomono) v3.001 | `assets/fonts/RobotoMono-Regular.ttf` | The Roboto Mono Project Authors | **SIL OFL 1.1** | [OFL-RobotoMono.txt](assets/fonts/OFL-RobotoMono.txt) |
| [esp-bsp `m5stack_tab5`](https://github.com/espressif/esp-bsp) 1.3.2, modified | `esp32/boards/m5stack_tab5/components/m5stack_tab5` | Espressif Systems | Apache-2.0 | its `LICENSE`; changes in its `VENDORED.md` |
| CMSIS Device, `system_stm32u5xx.c` modified | `stm32/board/startup_stm32u5a9njhxq.s`, `system_stm32u5xx.c` | ARM Limited, STMicroelectronics | Apache-2.0 | [LICENSE](LICENSE) |
| STM32CubeU5 configuration templates, settings changed | `stm32/board/aps512xx_conf.h`, `sitronix_conf.h`, `stm32u5x9j_discovery_conf.h`, `stm32u5xx_hal_conf.h` | STMicroelectronics | BSD-3-Clause | [stm32/board/LICENSE.md](stm32/board/LICENSE.md) |
| `json2rc.js`, modified | `tools/splash` | [RemoteCompose-Examples](https://github.com/hoford/RemoteCompose-Examples) | Apache-2.0 here | [LICENSE](LICENSE) |

As its licence asks, ThorVG's rasterizer carries this credit:

> Portions of this software are copyright © 1996-2000 The FreeType Project (www.freetype.org).
> All rights reserved.

`third_party/thorvg` also holds code this project doesn't compile, with its own licence files:
JerryScript (Apache-2.0), RapidJSON (MIT), libwebp (BSD-3-Clause, Google Inc.) and, for tests,
Catch2 (BSL-1.0).

## Register tables and sequences from vendor drivers

Some board drivers use values taken from these projects. The source comments say where.

| Used in | From | Licence | Text |
|---|---|---|---|
| Core2, PaperS3 and Tab5 boards: power, panel and touch sequences; PaperS3 waveforms | [M5GFX](https://github.com/m5stack/M5GFX), [M5Unified](https://github.com/m5stack/M5Unified) (M5Stack) | MIT | [licenses/MIT-M5Stack.txt](licenses/MIT-M5Stack.txt) |
| The `lgfx` parts of M5GFX: PaperS3 bus and panel, Core2 panel | [LovyanGFX](https://github.com/lovyan03/LovyanGFX) (lovyan03 and contributors) | FreeBSD | [licenses/LovyanGFX-license.txt](licenses/LovyanGFX-license.txt) |
| Waveshare AMOLED 1.8 board: pins, panel init | [Waveshare](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8) | Apache-2.0 | [LICENSE](LICENSE) |
| Waveshare LCD 1.28 board: panel init | Waveshare's `LCD_1in28` driver | not stated | |
| CrowPanel 5.79" board: pins, RAM setup, update codes | [Elecrow's factory firmware](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792) | not stated | |

## Used by the build, not in this repository

These are fetched or expected beside this repository, and they end up in the binaries you build.

| Software | Authors | Licence |
|---|---|---|
| RemoteCompose reference engine `rccore`, from [remotecompose-experiments](https://github.com/camaelon/remotecompose-experiments) | its authors | Apache-2.0 |
| [ESP-IDF](https://github.com/espressif/esp-idf), with the libraries it bundles, such as FreeRTOS, lwIP and Mbed TLS | Espressif Systems and others | Apache-2.0; see ESP-IDF's copyright notes for the rest |
| Espressif components: `bmi270`, `esp_codec_dev`, `esp_io_expander`, `esp_io_expander_pi4ioe5v6408`, `esp_lcd_ili9881c`, `esp_lcd_st7123`, `esp_lcd_touch`, `esp_lcd_touch_gt911`, `esp_lcd_touch_st7123` | Espressif Systems | Apache-2.0 |
| [STM32CubeU5](https://github.com/STMicroelectronics/STM32CubeU5) HAL and BSP | STMicroelectronics | BSD-3-Clause: [text](licenses/BSD-3-Clause-STMicroelectronics.txt) |
| CMSIS Core | ARM Limited | Apache-2.0 |
| [SDL2](https://www.libsdl.org) (Linux player) | Sam Lantinga and contributors | zlib |
| [zlib](https://zlib.net) (Linux player, host tools) | Jean-loup Gailly, Mark Adler | zlib |

Developer tools run but not shipped: `tools/esptool256.py` imports
[esptool](https://github.com/espressif/esptool) (GPL-2.0-or-later); `tools/compare.py` and
`tools/snap.py` use [NumPy](https://numpy.org) (BSD-3-Clause) and
[Pillow](https://python-pillow.org) (MIT-CMU); `tools/splash/compile.mjs` runs on Node.js.

## Standards and sample documents

Kobento implements the RemoteCompose format from Android (`androidx.compose.remote`, The Android
Open Source Project, Apache-2.0). Documents in `documents/` are derived from the androidx demos
and RemoteCompose-Examples, and they are under Apache-2.0 here. "Roboto" and "Roboto Mono" are
trademarks of Google.
