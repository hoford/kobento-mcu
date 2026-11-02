# M5Stack Tab5: hardware facts for ESP-IDF work

These facts were gathered on 2026-10-17 from:

- esp-bsp `bsp/m5stack_tab5` at commit f259a6a
- M5Tab5-UserDemo at commit b4e356b
- esp-hosted-mcu
- the IDF v6.0.1 Kconfig files
- docs.m5stack.com/en/core/Tab5

Items marked **(verified)** were checked on our own unit.

## Our unit
- The USB port is the P4's native USB Serial/JTAG (`303a:1001`) at `/dev/cu.usbmodem31101`. The serial number is the MAC, `30:ED:A0:E3:00:F0` **(verified)**.
- The ROM banner reads `ESP-ROM:esp32p4-eco2-20240710`, so the P4 is a **rev v1.x** chip **(verified)**.
- It came with a firmware that prints `rcNet` / `RECEIVER port 8937`. That firmware runs an HTTP server where `GET /info` returns `{"name":"M5Stack","version":1}`. Wi-Fi was up on the local network. We don't know where this firmware came from.

## Chip and memory: required sdkconfig on IDF v6
```
CONFIG_IDF_TARGET="esp32p4"
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y   # mandatory: v6 defaults to rev >= 3.1
CONFIG_ESP32P4_REV_MIN_100=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_SPIRAM_SPEED_200M=y             # 250M not allowed below rev 3
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
```
The unit has 32 MB of PSRAM and 16 MB of flash.

## Display: 720x1280 portrait over 2-lane MIPI-DSI
Three panel revisions exist. The sticker on the back names the driver chip.

| Revision | Panel | Touch | Lane bitrate | DPI clock |
|---|---|---|---|---|
| Rev 1 (2025-05) | ILI9881C | GT911 @ 0x14 | 1000 Mbps (M5 uses 730) | 60 MHz |
| Rev 2 (2025-10) | ST7123 | built-in @ 0x55 | 965 Mbps | 70 MHz |
| Rev 3 (2026-04) | ST7121 | built-in @ 0x55 | 965 Mbps | 70 MHz |

- **Runtime detection:**
  1. Release touch reset (expander 0x43 P5) and wait about 500 ms.
  2. If something answers at 0x55, read register 0x0000: a value of 3 means ST7123, and 1 means ST7121.
  3. If not, and the GT911 answers at 0x14, the board is an ILI9881C.
- **MIPI PHY power:** on-chip LDO channel 3 at 2500 mV.
- **Backlight:** GPIO22, LEDC PWM at 5 kHz, 10-bit.
- **LCD reset:** expander 0x43 pin P4. Never drive it high push-pull: assert reset with output 0, and release it by switching the pin to input with pull-up.
- The official BSP **`espressif/m5stack_tab5` 1.3.2** handles all three revisions and has IDF ≥ 6 code paths.

## I2C system bus (SDA=31, SCL=32)
| Address | Device |
|---|---|
| 0x10 | ES8388 codec |
| 0x14 | GT911 touch (rev 1) |
| 0x32 | RX8130CE RTC |
| 0x40 | ES7210 mic ADC |
| 0x41 | INA226 power monitor |
| 0x43 | PI4IOE5V6408 expander #1 |
| 0x44 | PI4IOE5V6408 expander #2 |
| 0x55 | ST7123/ST7121 touch |
| 0x68 | BMI270 IMU |

Expander pins:

| Expander | P0 | P1 | P2 | P3 | P4 | P5 | P6 | P7 |
|---|---|---|---|---|---|---|---|---|
| #1 (0x43) | antenna select (ext) | speaker amp enable | external 5V | — | LCD reset | touch reset | camera reset | headphone detect (input) |
| #2 (0x44) | **C6 power enable** | — | — | USB-A 5V | power-off pulse | quick charge (active low) | charge status (input) | charge enable |

The touch interrupt is on GPIO23. On rev 1 boards the BSP drives GPIO23 low as an output to work around a GT911 strapping problem.

## Wi-Fi/BLE: the ESP32-C6 co-processor over SDIO
- **SDIO pins:** CLK=12, CMD=13, D0=11, D1=10, D2=9, D3=8, 4-bit at 40 MHz. The C6 reset is GPIO15, active low.
- **Expander 0x44 comes up with pins high-impedance and pulled down.** A register read
  showed `highz=11111110 pull_en=11111111 pull_up=00000000`. A pin set to output high still
  drives nothing until its OUT_H_IM bit (reg 0x07) is cleared, and pull-downs hold inputs
  at 0. `>expander` on the Muse firmware console prints the registers.
  After clearing high-Z on P7/P5 and the pull on P6, the Tab5 charged at about 990 mA from a
  Mac USB port, and P6 read 1 **(verified)**. The pack measured 7.99 V while charging,
  against 7.79 V at rest.
- **Charging must be enabled by firmware.** The BSP resets expander 0x44, which leaves P7
  (CHG_EN) floating, so the battery may not charge. Drive P7 high, and P5 (nCHG_QC_EN) low
  for quick charge. M5Unified reads P6 (CHG_STAT) as high while charging, but on this unit
  it read 0 throughout an unplug/replug test **(verified)**, so the firmware takes the power
  state from the INA226 current instead. On battery the current was about +300 mA through
  the shunt (positive = discharging), with the pack sagging 7.80 → 7.73 V. On a Mac USB port
  it was about 1 mA: the pack was idle and **not charging**. This follows M5Unified
  `Power_Class.cpp`, which also configures the INA226 at 0x41 with a 5 mOhm shunt and
  16-sample averaging. The pack is 2S, so the voltage per cell is the bus voltage / 2.
  See `components/board/src/power.cpp` here.
- **The C6 must be powered first** (expander 0x44 P0) before esp_hosted starts. Otherwise the P4 crashes.
- **Factory C6 firmware:** esp-hosted-mcu slave "V1.4.1". It includes the BLE controller with HCI over SDIO (BLE only).
- **Host side:** `espressif/esp_hosted` 3.x plus `esp_wifi_remote`. A 3.x host is documented as back-compatible with a 1.x slave, but a matching slave is recommended.
  - Enable BLE with `CONFIG_ESP_HOSTED_HOST_FEAT_BT` and `CONFIG_BT_NIMBLE_ENABLED`.
  - Call `esp_hosted_bt_host_stack_setup()` before `nimble_port_init()`.
- **Verified 2026-10-17:** BLE works with IDF v6 + esp_hosted 3.0.9 + the factory C6 image (NimBLE scans and advertises beside Wi-Fi), but only with `bring_up_controller = false`: the old image never answers the controller-init RPC (msg 387) and already runs its controller.
- **Cold boot:** esp_hosted's before-`app_main` constructor talks to the C6 before anything can power it (expander 0x44 P0). Set `CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN=n`, power the C6, then call `esp_hosted_init()` and `esp_hosted_connect_to_slave()`.

## Audio and other peripherals
- **Audio:** ES8388 DAC and ES7210 mics. I2S pins: MCLK=30, SCLK=27, LRCK=29, DOUT=26, DIN=28. The speaker amp is enabled from expander 0x43 P1.
- **No user buttons:** the only keys are Reset/Boot and Power.
- **SD card:** SDMMC D0–3 on 39–42, CLK=43, CMD=44.
- **RS485:** RX=21, TX=20, DIR=34.
- **Grove:** GPIO 53/54.
