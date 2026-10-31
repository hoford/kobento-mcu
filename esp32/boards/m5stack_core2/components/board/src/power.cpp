// Core2 power: the internal I2C bus, the AXP192 (v1.0) / AXP2101 (v1.1) PMU, and the
// battery readings for rcstage's device-state sampler.
//
// Register sequences follow M5Unified (utility/Power_Class.cpp, power/AXP192_Class.cpp,
// power/AXP2101_Class.cpp) and M5GFX (M5GFX.cpp, board_M5StackCore2).
#include "core2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "board/board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "core2";

namespace board {

static i2c_master_bus_handle_t sBus;
static std::map<uint8_t, i2c_master_dev_handle_t> sDevs;
static Pmu sPmu = Pmu::None;
static i2c_master_dev_handle_t sAxp, sIna;

i2c_master_bus_handle_t i2cBus() {
    if (sBus) return sBus;
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_1;
    cfg.sda_io_num = GPIO_NUM_21;
    cfg.scl_io_num = GPIO_NUM_22;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&cfg, &sBus) != ESP_OK) {
        ESP_LOGE(TAG, "internal I2C bus failed");
        sBus = nullptr;
    }
    return sBus;
}

i2c_master_dev_handle_t i2cDevice(uint8_t addr) {
    auto it = sDevs.find(addr);
    if (it != sDevs.end()) return it->second;
    if (!i2cBus()) return nullptr;
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = addr;
    dev.scl_speed_hz = 400000;
    i2c_master_dev_handle_t h = nullptr;
    if (i2c_master_bus_add_device(sBus, &dev, &h) != ESP_OK) h = nullptr;
    sDevs[addr] = h;
    return h;
}

bool i2cRead(i2c_master_dev_handle_t d, uint8_t reg, uint8_t* out, size_t n) {
    return d && i2c_master_transmit_receive(d, &reg, 1, out, n, 50) == ESP_OK;
}

bool i2cWrite(i2c_master_dev_handle_t d, uint8_t reg, uint8_t value) {
    const uint8_t b[2] = {reg, value};
    return d && i2c_master_transmit(d, b, 2, 50) == ESP_OK;
}

static uint8_t rd(uint8_t reg) {
    uint8_t v = 0;
    i2cRead(sAxp, reg, &v, 1);
    return v;
}
static void wr(uint8_t reg, uint8_t v) { i2cWrite(sAxp, reg, v); }
static void setBits(uint8_t reg, uint8_t bits) { wr(reg, rd(reg) | bits); }
static void clearBits(uint8_t reg, uint8_t bits) { wr(reg, rd(reg) & ~bits); }

static void axp192Init() {
    // M5GFX: GPIO4 (LCD + touch reset) as GPIO, LDO2 (LCD logic, SD) 3.3 V on.
    wr(0x95, (rd(0x95) & 0x72) | 0x84);
    setBits(0x28, 0xF0);
    setBits(0x12, 0x04);
    // M5Unified Power.begin. 0x12 is not rewritten: that would drop EXTEN after a reset.
    static const uint8_t kRegs[][2] = {
        {0x26, 0x6A},   // DCDC1 (ESP32) 3350 mV
        {0x30, 0x02},   // VBUS-IPSOUT
        {0x31, 0x04},   // VOFF 3.0 V
        {0x32, 0x42},   // battery detection on
        {0x33, 0xC0},   // charging on, 4.2 V
        {0x35, 0xA2},   // RTC backup battery charging
        {0x36, 0x0C},   // power key: 128 ms on, 4 s off
        {0x40, 0x00}, {0x41, 0x00}, {0x42, 0x03}, {0x43, 0x00}, {0x44, 0x00},   // IRQs: PEK only
        {0x82, 0xFF},   // all ADCs on
        {0x83, 0x80},   // temperature ADC on
        {0x84, 0x32},   // ADC 25 Hz
        {0x90, 0x07},   // GPIO0 / LDOio0 floating
        {0x91, 0xA0},   // LDOio0 2.8 V
        {0x98, 0x00}, {0x99, 0xFF}, {0x9A, 0xFF},   // PWM1 (green LED) off
    };
    for (auto& r : kRegs) wr(r[0], r[1]);
    // Core2: vibration motor (LDO3) off, speaker amp (GPIO2) off, green LED on PWM1 off,
    // charge current ~360 mA.
    wr(0x28, rd(0x28) & 0xF0);
    clearBits(0x12, 0x08);
    wr(0x93, 0x05);
    wr(0x92, 0x02);
    wr(0x33, (rd(0x33) & 0xF0) | 0x06);
    // LCD reset: GPIO4 low, then high.
    clearBits(0x96, 0x02);
    vTaskDelay(pdMS_TO_TICKS(2));
    setBits(0x96, 0x02);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void axp2101Init() {
    // M5GFX: ALDO4 (TF card, touch, LCD power) on; ALDO3 (speaker) and DLDO1 (motor) off;
    // DCDC1 and DCDC3 3.3 V.
    wr(0x90, (rd(0x90) & 0x7B) | 0x08);
    setBits(0x80, 0x05);
    wr(0x82, 0x12);
    wr(0x84, 0x6A);
    // M5Unified Power.begin.
    static const uint8_t kRegs[][2] = {
        {0x27, 0x00},   // power key: hold 1 s on, 4 s off
        {0x10, 0x30},
        {0x12, 0x00},   // BATFET disabled
        {0x68, 0x01},   // battery detection on
        {0x69, 0x13},   // CHGLED
        {0x99, 0x00},   // DLDO1 (motor) voltage 0
        {0x30, 0x0F},   // ADCs on
    };
    for (auto& r : kRegs) wr(r[0], r[1]);
    // LCD and touch reset is ALDO2 (not 0x96 bit 1, which M5GFX toggles on both revisions:
    // on the AXP2101 that register is BLDO1, the backlight voltage). ALDO2 3.3 V, off, on.
    wr(0x93, 0x1C);
    clearBits(0x90, 0x02);
    vTaskDelay(pdMS_TO_TICKS(2));
    setBits(0x90, 0x02);
    vTaskDelay(pdMS_TO_TICKS(20));
}

Pmu pmuInit() {
    sAxp = i2cDevice(0x34);
    uint8_t id = 0;
    if (!i2cRead(sAxp, 0x03, &id, 1)) {
        ESP_LOGE(TAG, "no PMU at 0x34");
        return sPmu = Pmu::None;
    }
    if (id == 0x4A) {
        sPmu = Pmu::Axp2101;
        axp2101Init();
        sIna = i2cDevice(0x40);
        uint8_t ina[2] = {};
        if (!i2cRead(sIna, 0xFF, ina, 2) || ((ina[0] << 8) | ina[1]) != 0x3220) sIna = nullptr;
    } else {
        sPmu = Pmu::Axp192;
        axp192Init();
    }
    ESP_LOGI(TAG, "Core2 %s, PMU %s (id 0x%02X)%s", sPmu == Pmu::Axp2101 ? "v1.1" : "v1.0",
             sPmu == Pmu::Axp2101 ? "AXP2101" : "AXP192", id, sIna ? ", INA3221" : "");
    return sPmu;
}

Pmu pmu() { return sPmu; }

void pmuBacklight(int percent) {
    const int b = percent <= 0 ? 0 : std::min(255, percent * 255 / 100);
    if (sPmu == Pmu::Axp192) {
        // DCDC3 2500..3275 mV.
        if (b == 0) { clearBits(0x12, 0x02); return; }
        wr(0x27, (rd(0x27) & 0x80) | ((b >> 3) + 72));
        setBits(0x12, 0x02);
    } else if (sPmu == Pmu::Axp2101) {
        // BLDO1 2.5..3.3 V.
        if (b == 0) { clearBits(0x90, 0x10); return; }
        wr(0x96, (b + 641) >> 5);
        setBits(0x90, 0x10);
    }
}

// ── The power key ──

int buttonPressed() {
    if (int step = touchMarkStep()) return step;   // the A / B / C marks under the screen
    // The PMU latches a press until it is cleared, so a 100 ms poll misses none; reading
    // every frame would only add I2C traffic next to the touch controller's.
    static int64_t next = 0;
    const int64_t now = esp_timer_get_time();
    if (now < next || sPmu == Pmu::None) return 0;
    next = now + 100000;
    // Bit 1 (AXP192) / bit 3 (AXP2101): a short press; the bit below it, a long one. Write
    // 1s to clear, as M5Unified's getPekPress() does.
    const uint8_t reg = sPmu == Pmu::Axp192 ? 0x46 : 0x49;
    const uint8_t mask = sPmu == Pmu::Axp192 ? 0x03 : 0x0C;
    const uint8_t shortBit = sPmu == Pmu::Axp192 ? 0x02 : 0x08;
    const uint8_t v = rd(reg) & mask;
    if (v) wr(reg, v);
    return (v & shortBit) ? 1 : 0;
}

// ── Motion: the MPU6886 (0x68) isn't read yet ──

Motion readMotion() { return {}; }

// ── Battery ──

void powerInit() {}   // pmuInit() ran from displayInit()

static uint16_t rd12(uint8_t reg) {   // AXP192: high byte then low nibble
    uint8_t b[2] = {};
    i2cRead(sAxp, reg, b, 2);
    return (uint16_t) ((b[0] << 4) | (b[1] & 0x0F));
}
static uint16_t rd13(uint8_t reg) {
    uint8_t b[2] = {};
    i2cRead(sAxp, reg, b, 2);
    return (uint16_t) ((b[0] << 5) | (b[1] & 0x1F));
}
static uint16_t rd14(uint8_t reg) {   // AXP2101
    uint8_t b[2] = {};
    i2cRead(sAxp, reg, b, 2);
    return (uint16_t) (((b[0] & 0x3F) << 8) | b[1]);
}

// A single Li-ion cell, 3.3..4.15 V, the same curve M5Unified uses.
static int levelFromMillivolts(float mv) {
    return (int) std::lround(std::fmax(0.0, std::fmin(100.0, (mv - 3300) * 100 / 800)));
}

Power readPower() {
    Power p;
    if (sPmu == Pmu::Axp192) {
        const uint8_t s0 = rd(0x00), s1 = rd(0x01);
        const bool battery = s1 & 0x20;
        const bool external = s0 & 0xA0;       // ACIN or VBUS present
        p.valid = true;
        p.charging = s0 & 0x04;
        p.batteryVolts = rd12(0x78) * 1.1e-3f;
        p.batteryMilliamps = (rd13(0x7A) - (float) rd13(0x7C)) * 0.5f;
        p.batteryLevel = battery ? levelFromMillivolts(p.batteryVolts * 1000) : -1;
        p.powerState = !battery ? 2 : p.charging ? 1 : external ? 2 : 0;
    } else if (sPmu == Pmu::Axp2101) {
        const uint8_t s0 = rd(0x00), s1 = rd(0x01);
        const bool battery = s0 & 0x08;
        const bool external = s0 & 0x20;       // VBUS good
        const int dir = (s1 >> 5) & 3;         // 1 charging, 2 discharging, 0 standby
        p.valid = true;
        p.charging = dir == 1;
        p.batteryVolts = rd14(0x34) * 1e-3f;
        const uint8_t gauge = rd(0xA4);
        p.batteryLevel = battery ? (gauge <= 100 ? gauge : levelFromMillivolts(p.batteryVolts * 1000)) : -1;
        if (sIna) {   // CH1 is the battery: shunt LSB 5 uV, 10 mOhm shunt
            uint8_t b[2] = {};
            if (i2cRead(sIna, 0x01, b, 2)) p.batteryMilliamps = -(int16_t) ((b[0] << 8) | b[1]) * 0.5f;
        }
        p.powerState = !battery ? 2 : p.charging ? 1 : external ? 2 : 0;
    }
    return p;
}

void touchProbe();   // display.cpp

bool console(const std::string& line) {
    if (line == "touch") { touchProbe(); return true; }
    if (line != "pmu") return false;
    if (sPmu == Pmu::None) { printf("pmu: none\n"); return true; }
    printf("pmu: %s\n", sPmu == Pmu::Axp192 ? "AXP192 (Core2 v1.0)" : "AXP2101 (Core2 v1.1)");
    for (int r = 0; r < 0x100; r += 16) {
        printf("%02X:", r);
        for (int i = 0; i < 16; i++) printf(" %02X", rd(r + i));
        printf("\n");
    }
    return true;
}

// Not on this board (yet): no RTC driver, nothing to beep with, a panel that needs no refresh.
bool rtcRead(int64_t*) { return false; }
bool rtcWrite(int64_t) { return false; }
bool beep(int, int) { return false; }
bool displayRefresh() { return false; }

const char* wakeReason() { return "power_on"; }
bool sleepFor(int, Sleep) { return false; }

}  // namespace board
