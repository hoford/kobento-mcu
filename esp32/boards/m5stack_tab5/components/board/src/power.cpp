// Tab5 power: INA226 battery monitor, the charger behind IO expander 0x44, headphone detect.
// The shared sampler (rcstage device_state) calls readPower() once a second.
#include "board/board.h"

#include <cmath>
#include <cstdio>
#include <mutex>

#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "power";

namespace board {

// INA226 power monitor on the system bus. Bus voltage LSB 1.25 mV; shunt voltage LSB 2.5 uV.
static constexpr uint8_t kIna226 = 0x41;
// The shunt on the battery path, and the configuration M5Unified uses (Power_Class.cpp):
// 16-sample averaging, 1.1 ms bus and shunt conversions, continuous shunt + bus.
static constexpr float kShuntOhms = 0.005f;
static constexpr uint16_t kInaConfig = 0x4527;
// The pack's internal resistance, measured (2026-10-18): 7.79 V at rest, 7.99 V charging at
// 0.99 A, about 0.055 V lower discharging at 0.30 A.
static constexpr float kPackOhms = 0.2f;
// Below this the pack is neither charging nor powering the Tab5: on external power.
static constexpr float kIdleMilliamps = 20.0f;
// Expander 0x44: P7 CHG_EN (output, high enables the charger), P6 CHG_STAT (input, high
// while charging), P5 nCHG_QC_EN (output, low enables quick charge). 0x43 P7 headphone
// detect. The BSP resets the expanders and leaves P7 floating, so charging must be enabled
// here, as M5Unified does at boot.
static constexpr uint32_t kChargeEnablePin = IO_EXPANDER_PIN_NUM_7;
static constexpr uint32_t kChargePin = IO_EXPANDER_PIN_NUM_6;
static constexpr uint32_t kQuickChargePin = IO_EXPANDER_PIN_NUM_5;
static constexpr uint32_t kHeadphonePin = IO_EXPANDER_PIN_NUM_7;

static i2c_master_dev_handle_t sIna;
static std::mutex sLock;

// The last two minutes of raw power readings, for `>sys history` after USB was unplugged
// (which also takes the console away).
struct Raw {
    uint32_t t;            // s since boot
    uint16_t busRaw;       // INA226 bus voltage register
    int16_t shuntRaw;      // INA226 shunt voltage register
    uint8_t ex44;          // expander 0x44 input levels
};
static Raw sHistory[120];
static int sHistoryNext = 0, sHistoryCount = 0;

static bool inaRead(uint8_t reg, int16_t& out) {
    uint8_t b[2];
    if (i2c_master_transmit_receive(sIna, &reg, 1, b, 2, 50) != ESP_OK) return false;
    out = (int16_t) ((b[0] << 8) | b[1]);
    return true;
}

// A 2S Li-ion pack: the per-cell open-circuit curve, 0..100 %.
static int levelFromVolts(float packVolts) {
    static const float kCell[][2] = {{3.30f, 0}, {3.60f, 10}, {3.70f, 30}, {3.80f, 55},
                                     {3.90f, 75}, {4.00f, 88}, {4.10f, 96}, {4.20f, 100}};
    const float v = packVolts / 2;
    if (v <= kCell[0][0]) return 0;
    for (size_t i = 1; i < sizeof(kCell) / sizeof(kCell[0]); i++) {
        if (v <= kCell[i][0]) {
            float t = (v - kCell[i - 1][0]) / (kCell[i][0] - kCell[i - 1][0]);
            return (int) std::lround(kCell[i - 1][1] + t * (kCell[i][1] - kCell[i - 1][1]));
        }
    }
    return 100;
}

// Read-modify-write one register of expander 0x44, straight to the chip.
static bool expanderClear(uint8_t reg, uint8_t clear) {
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = 0x44;
    dev.scl_speed_hz = 400000;
    i2c_master_dev_handle_t h;
    if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &dev, &h) != ESP_OK) return false;
    uint8_t v = 0;
    bool ok = i2c_master_transmit_receive(h, &reg, 1, &v, 1, 50) == ESP_OK;
    if (ok) {
        const uint8_t w[2] = {reg, (uint8_t) (v & ~clear)};
        ok = i2c_master_transmit(h, w, 2, 50) == ESP_OK;
    }
    i2c_master_bus_rm_device(h);
    return ok;
}

void powerInit() {
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = kIna226;
    dev.scl_speed_hz = 400000;
    if (bsp_i2c_init() != ESP_OK ||
        i2c_master_bus_add_device(bsp_i2c_get_handle(), &dev, &sIna) != ESP_OK) {
        ESP_LOGW(TAG, "INA226 not reachable: no battery data");
        sIna = nullptr;
    } else {
        const uint8_t cfg[3] = {0x00, kInaConfig >> 8, kInaConfig & 0xFF};
        if (i2c_master_transmit(sIna, cfg, sizeof cfg, 50) != ESP_OK) ESP_LOGW(TAG, "INA226 config failed");
    }
    if (auto ex = bsp_io_expander1_init()) {
        esp_io_expander_set_dir(ex, kChargePin, IO_EXPANDER_INPUT);
        esp_io_expander_set_dir(ex, kChargeEnablePin | kQuickChargePin, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(ex, kQuickChargePin, 0);
        esp_io_expander_set_level(ex, kChargeEnablePin, 1);
        // The pins also start out high-impedance with pull-downs (read back: highz=11111110,
        // pull_en=11111111, pull_up=0), so P7 never reached the charger and P6 read 0. Clear
        // high-Z on P7/P5 and the pull on P6, as M5Unified configures them; leave other bits.
        bool ok = expanderClear(0x07, kChargeEnablePin | kQuickChargePin)   // OUT_H_IM
               && expanderClear(0x0B, kChargePin);                          // PULL_EN
        ESP_LOGI(TAG, "charger enabled%s", ok ? "" : " (expander write failed)");
    }
}

Power readPower() {
    Power p;
    int16_t bus = 0, shunt = 0;
    p.valid = sIna && inaRead(0x02, bus) && inaRead(0x01, shunt);
    if (p.valid) {
        p.batteryVolts = (uint16_t) bus * 1.25e-3f;
        // The shunt reads positive while the pack discharges; report + into the battery.
        p.batteryMilliamps = -shunt * 2.5e-6f / kShuntOhms * 1000.0f;
    }
    // The power state comes from the current: about 300 mA out on battery, about 0 on USB
    // with the pack idle or full, about 1 A in while charging. CHG_STAT (0x44 P6) agrees once
    // its pull-down is off, but the current also tells "external power" apart.
    uint32_t lv = 0, ex44 = 0;
    if (auto ex = bsp_io_expander1_init()) esp_io_expander_get_level(ex, 0xFF, &ex44);
    p.charging = p.valid && p.batteryMilliamps > kIdleMilliamps;
    if (auto ex = bsp_io_expander_init(); ex && esp_io_expander_get_level(ex, kHeadphonePin, &lv) == ESP_OK)
        p.headphones = (lv & kHeadphonePin) != 0;
    {
        std::lock_guard<std::mutex> g(sLock);
        sHistory[sHistoryNext] = {(uint32_t) (esp_timer_get_time() / 1000000), (uint16_t) bus, shunt,
                                  (uint8_t) ex44};
        sHistoryNext = (sHistoryNext + 1) % 120;
        if (sHistoryCount < 120) sHistoryCount++;
    }
    const bool battery = p.valid && p.batteryVolts > 5.0f;   // a 2S pack never sits below ~6 V
    // The open-circuit curve needs the resting voltage: take out what the current adds or
    // drops across the pack's internal resistance.
    p.batteryLevel = battery ? levelFromVolts(p.batteryVolts - p.batteryMilliamps / 1000.0f * kPackOhms)
                             : -1;
    p.powerState = !p.valid ? -1
                 : !battery || std::fabs(p.batteryMilliamps) <= kIdleMilliamps ? 2
                 : p.charging ? 1 : 0;
    return p;
}

static void history() {
    std::lock_guard<std::mutex> g(sLock);
    printf("t(s)  bus(V)  in(mA)  0x44 in(bin)  P6\n");
    for (int i = 0; i < sHistoryCount; i++) {
        const Raw& r = sHistory[(sHistoryNext - sHistoryCount + i + 120) % 120];
        char bits[9];
        for (int b = 0; b < 8; b++) bits[b] = (r.ex44 >> (7 - b)) & 1 ? '1' : '0';
        bits[8] = 0;
        printf("%5u  %6.3f  %8.1f  %s   %d\n", (unsigned) r.t, r.busRaw * 1.25e-3f,
               -r.shuntRaw * 2.5e-6f / kShuntOhms * 1000.0f, bits, (r.ex44 & kChargePin) != 0);
    }
}

static void expanderDump() {
    // Raw PI4IOE5V6408 registers, read straight from the chips (the driver caches its writes).
    for (uint8_t addr : {(uint8_t) 0x43, (uint8_t) 0x44}) {
        i2c_device_config_t dev = {};
        dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev.device_address = addr;
        dev.scl_speed_hz = 400000;
        i2c_master_dev_handle_t h;
        if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &dev, &h) != ESP_OK) continue;
        printf("0x%02X:", addr);
        static const struct { uint8_t reg; const char* name; } kRegs[] = {
            {0x03, "dir"}, {0x05, "out"}, {0x07, "highz"}, {0x0B, "pull_en"}, {0x0D, "pull_up"},
            {0x0F, "in"}};
        for (auto& r : kRegs) {
            uint8_t v = 0;
            bool ok = i2c_master_transmit_receive(h, &r.reg, 1, &v, 1, 50) == ESP_OK;
            printf(" %s=%s", r.name, ok ? "" : "?");
            if (ok) for (int b = 7; b >= 0; b--) putchar((v >> b) & 1 ? '1' : '0');
        }
        printf("\n");
        i2c_master_bus_rm_device(h);
    }
}

bool extrasConsole(const std::string& line);   // extras.cpp: imu, rtc, beep

bool console(const std::string& line) {
    if (extrasConsole(line)) return true;
    if (line == "sys history") history();
    else if (line == "expander") expanderDump();
    else return false;
    return true;
}

}  // namespace board
