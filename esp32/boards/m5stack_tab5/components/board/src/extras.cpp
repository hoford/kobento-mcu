// Tab5 extras: the BMI270 IMU, the RX8130 RTC and the speaker (ES8388 codec), all on the
// BSP's system I2C bus.
#include "board/board.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <mutex>

#include "bmi270.h"
#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* TAG = "tab5";

namespace board {

// ── Motion: the BMI270 (0x68), through espressif/bmi270 (it uploads the chip's config
// file). Started on first use. ──

// Screen axis <- chip axis and sign; see "imu" on the console to check them.
static constexpr int kAxisX = 0, kAxisY = 1, kAxisZ = 2;
static constexpr float kSignX = 1, kSignY = 1, kSignZ = -1;   // the chip faces the back
static constexpr float kG = 9.80665f, kRad = 3.14159265f / 180;

static bmi270_handle_t* sImu;

static bool imuUp() {
    static std::mutex lock;
    static bool tried = false;
    std::lock_guard<std::mutex> g(lock);
    if (sImu || tried) return sImu != nullptr;
    tried = true;
    if (bsp_i2c_init() != ESP_OK) return false;
    bmi270_driver_config_t dc = {};
    dc.addr = BMI270_I2C_ADDRESS_L;
    dc.interface = BMI270_USE_I2C;
    dc.i2c_bus = bsp_i2c_get_handle();
    bmi270_handle_t* h = nullptr;
    if (bmi270_create(&dc, &h) != ESP_OK) { ESP_LOGW(TAG, "BMI270 not found"); return false; }
    bmi270_config_t c = {};
    c.acce_odr = BMI270_ACC_ODR_100_HZ;
    c.acce_range = BMI270_ACC_RANGE_4_G;
    c.gyro_odr = BMI270_GYR_ODR_100_HZ;
    c.gyro_range = BMI270_GYR_RANGE_1000_DPS;
    if (bmi270_start(h, &c) != ESP_OK) { ESP_LOGW(TAG, "BMI270 didn't start"); bmi270_delete(h); return false; }
    // Reads are zero until the first sample is in (a few ms at 100 Hz, after the start-up).
    for (int i = 0; i < 20; i++) {
        float x = 0, y = 0, z = 0;
        if (bmi270_get_acce_data(h, &x, &y, &z) == ESP_OK && (x != 0 || y != 0 || z != 0)) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    sImu = h;
    ESP_LOGI(TAG, "BMI270 up");
    return true;
}

static bool imuRaw(float a[3], float g[3]) {
    return imuUp() && bmi270_get_acce_data(sImu, &a[0], &a[1], &a[2]) == ESP_OK &&
           bmi270_get_gyro_data(sImu, &g[0], &g[1], &g[2]) == ESP_OK;
}

Motion readMotion() {
    Motion m;
    float a[3], g[3];
    if (!imuRaw(a, g)) return m;
    m.valid = true;
    m.ax = kSignX * a[kAxisX] * kG; m.ay = kSignY * a[kAxisY] * kG; m.az = kSignZ * a[kAxisZ] * kG;
    m.gx = kSignX * g[kAxisX] * kRad; m.gy = kSignY * g[kAxisY] * kRad; m.gz = kSignZ * g[kAxisZ] * kRad;
    return m;
}

// ── Clock: the RX8130CE RTC (0x32), kept in UTC. Set up as M5Unified does (RX8130_Class). ──

static i2c_master_dev_handle_t sRtc;

static bool rtcReg(uint8_t reg, uint8_t* out, size_t n) {
    return i2c_master_transmit_receive(sRtc, &reg, 1, out, n, 50) == ESP_OK;
}

static bool rtcUp() {
    static std::mutex lock;
    std::lock_guard<std::mutex> g(lock);
    if (sRtc) return true;
    if (bsp_i2c_init() != ESP_OK) return false;
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = 0x32;
    dev.scl_speed_hz = 400000;
    i2c_master_dev_handle_t h;
    if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &dev, &h) != ESP_OK) return false;
    uint8_t ctl1 = 0;
    const uint8_t r = 0x1F;
    if (i2c_master_transmit_receive(h, &r, 1, &ctl1, 1, 50) != ESP_OK) {
        i2c_master_bus_rm_device(h);
        return false;
    }
    // Control 1: battery backup on (0x30); function register cleared; no interrupts.
    const uint8_t a[2] = {0x1F, (uint8_t) (ctl1 | 0x30)}, b[2] = {0x30, 0x00}, c[2] = {0x1E, 0x00};
    i2c_master_transmit(h, a, 2, 50);
    i2c_master_transmit(h, b, 2, 50);
    i2c_master_transmit(h, c, 2, 50);
    sRtc = h;
    return true;
}

static int fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t toBcd(int v) { return (uint8_t) ((v / 10) << 4 | v % 10); }

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's days_from_civil).
static int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t) era * 146097 + doe - 719468;
}

bool rtcRead(int64_t* epoch) {
    uint8_t b[7], flags = 0;
    if (!rtcUp() || !rtcReg(0x10, b, 7) || !rtcReg(0x1D, &flags, 1)) return false;
    if (flags & 0x02) return false;   // VLF: the clock lost power at some point
    const int year = fromBcd(b[6]) + 2000;
    const int64_t days = daysFromCivil(year, fromBcd(b[5] & 0x1F), fromBcd(b[4] & 0x3F));
    *epoch = days * 86400 + fromBcd(b[2] & 0x3F) * 3600 + fromBcd(b[1] & 0x7F) * 60 + fromBcd(b[0] & 0x7F);
    return year >= 2024;
}

bool rtcWrite(int64_t epoch) {
    if (!rtcUp()) return false;
    const time_t t = (time_t) epoch;
    struct tm u;
    gmtime_r(&t, &u);
    const uint8_t b[8] = {0x10, toBcd(u.tm_sec), toBcd(u.tm_min), toBcd(u.tm_hour),
                          (uint8_t) (1u << (u.tm_wday & 7)), toBcd(u.tm_mday), toBcd(u.tm_mon + 1),
                          toBcd(u.tm_year % 100)};
    const uint8_t clear[2] = {0x1D, 0x00};   // flags, VLF among them: the time is good again
    return i2c_master_transmit(sRtc, b, 8, 50) == ESP_OK && i2c_master_transmit(sRtc, clear, 2, 50) == ESP_OK;
}

// ── Sound: a sine on the speaker, through the BSP's ES8388 codec at 48 kHz mono. A task
// plays it, so beep() returns at once; the codec stays open after the first tone. ──

struct Tone { int hz, ms; };
static QueueHandle_t sTones;

static void toneTask(void*) {
    esp_codec_dev_handle_t spk = bsp_audio_codec_speaker_init();
    if (spk) {
        esp_codec_dev_sample_info_t fs = {};
        fs.bits_per_sample = 16;
        fs.channel = 1;
        fs.sample_rate = 48000;
        if (esp_codec_dev_open(spk, &fs) != ESP_CODEC_DEV_OK) spk = nullptr;
        else esp_codec_dev_set_out_vol(spk, 60);
    }
    if (!spk) ESP_LOGW(TAG, "speaker didn't start");
    static int16_t buf[480];   // 10 ms
    Tone t;
    while (true) {
        xQueueReceive(sTones, &t, portMAX_DELAY);
        if (!spk) continue;
        float phase = 0;
        const float step = 2 * 3.14159265f * t.hz / 48000;
        const int chunks = std::max(1, t.ms / 10);
        for (int c = 0; c < chunks && uxQueueMessagesWaiting(sTones) == 0; c++) {
            for (int i = 0; i < 480; i++) {
                // 2 ms fade in and out: no click
                const int n = c * 480 + i, total = chunks * 480;
                const float env = std::min(1.0f, std::min(n, total - n) / 96.0f);
                buf[i] = (int16_t) (std::sin(phase) * 12000 * env);
                phase += step;
                if (phase > 2 * 3.14159265f) phase -= 2 * 3.14159265f;
            }
            esp_codec_dev_write(spk, buf, sizeof buf);
        }
    }
}

bool beep(int hz, int ms) {
    static std::mutex lock;
    std::lock_guard<std::mutex> g(lock);
    if (!sTones) {
        sTones = xQueueCreate(1, sizeof(Tone));
        if (!sTones || xTaskCreate(toneTask, "tone", 4096, nullptr, 4, nullptr) != pdPASS) return false;
    }
    const Tone t{hz, ms};
    xQueueOverwrite(sTones, &t);
    return true;
}

bool displayRefresh() { return false; }

bool extrasConsole(const std::string& line) {
    if (line == "imu") {
        // Raw chip axes (g), to check kAxis*/kSign*: lying face up, screen z should be +1 g.
        for (int i = 0; i < 20; i++) {
            float a[3], g[3];
            if (!imuRaw(a, g)) { printf("imu: no BMI270\n"); break; }
            const Motion m = readMotion();
            printf("imu chip a %6.2f %6.2f %6.2f g | screen a %6.2f %6.2f %6.2f m/s2\n", a[0], a[1], a[2],
                   m.ax, m.ay, m.az);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        return true;
    }
    if (line == "rtc") {
        int64_t e = 0;
        const bool ok = rtcRead(&e);
        printf("rtc: %s %lld\n", ok ? "valid" : "invalid", (long long) e);
        return true;
    }
    if (line == "beep") {
        printf("beep: %s\n", beep(880, 300) ? "ok" : "no speaker");
        return true;
    }
    return false;
}

const char* wakeReason() { return "power_on"; }
bool sleepFor(int, Sleep) { return false; }

}  // namespace board
