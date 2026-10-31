// Waveshare ESP32-S3-Touch-AMOLED-1.8: display (SH8601 / CO5300 AMOLED on QSPI), touch
// (FT3168 / CST816 on I2C), power (AXP2101 on I2C), buttons (the PMU's PWR key and BOOT),
// console (USB Serial/JTAG).
//
// Pins and the panel init are Waveshare's (github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8,
// examples/esp-idf/13_display_colorbar, read 2026-10-19). The Arduino variant
// (arduino-esp32 variants/waveshare_esp32_s3_touch_amoled_18) has SDA and SCL the other way
// round; Waveshare's ESP-IDF code (SDA 15, SCL 14) is the one used here.
#include "board/board.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"

static const char* TAG = "amoled";

namespace board {

static constexpr gpio_num_t kLcdCs = GPIO_NUM_12, kLcdClk = GPIO_NUM_11, kLcdD0 = GPIO_NUM_4,
                            kLcdD1 = GPIO_NUM_5, kLcdD2 = GPIO_NUM_6, kLcdD3 = GPIO_NUM_7,
                            kSda = GPIO_NUM_15, kScl = GPIO_NUM_14, kBoot = GPIO_NUM_0;
static constexpr uint8_t kFt3168 = 0x38, kCst816 = 0x15, kAxp2101 = 0x34, kQmi8658 = 0x6B;

static constexpr int kLcdHz = 40 * 1000 * 1000;
static constexpr int kStripLines = 32;   // even: the panel wants even-aligned windows
static constexpr size_t kStripBytes = (size_t) kWidth * kStripLines * 2;

static esp_lcd_panel_io_handle_t sIo;
static uint16_t* sStrip[2];
static SemaphoreHandle_t sFree;   // counts strip buffers not on the wire
static const uint32_t* sLast;     // last presented frame, for dumpFrame
static int sLastW, sLastH;
static i2c_master_bus_handle_t sBus;
static i2c_master_dev_handle_t sTouch, sPmu, sImu;
static bool sV2;                  // CO5300 + CST816: the picture starts 16 columns in
static int sXGap;

// ── I2C (touch 0x38 or 0x15, PMU 0x34, IMU 0x6B, RTC 0x51) ──

static i2c_master_dev_handle_t i2cDevice(uint8_t addr) {
    if (!sBus) {
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port = I2C_NUM_0;
        cfg.sda_io_num = kSda;
        cfg.scl_io_num = kScl;
        cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt = 7;
        cfg.flags.enable_internal_pullup = true;
        if (i2c_new_master_bus(&cfg, &sBus) != ESP_OK) return nullptr;
    }
    if (i2c_master_probe(sBus, addr, 50) != ESP_OK) return nullptr;
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = addr;
    dev.scl_speed_hz = 400000;
    i2c_master_dev_handle_t h = nullptr;
    i2c_master_bus_add_device(sBus, &dev, &h);
    return h;
}

static bool i2cRead(i2c_master_dev_handle_t d, uint8_t reg, uint8_t* out, size_t n) {
    return d && i2c_master_transmit_receive(d, &reg, 1, out, n, 50) == ESP_OK;
}

static bool i2cWrite(i2c_master_dev_handle_t d, uint8_t reg, uint8_t value) {
    const uint8_t b[2] = {reg, value};
    return d && i2c_master_transmit(d, b, 2, 50) == ESP_OK;
}

static uint8_t pmuRead(uint8_t reg) {
    uint8_t v = 0;
    i2cRead(sPmu, reg, &v, 1);
    return v;
}

// ── Motion: QMI8658, set up as Waveshare's SensorLib does: reset (0x60 = 0xB0), address
// auto-increment (CTRL1 bit 6, little-endian data), accelerometer +-4 g at 250 Hz (CTRL2
// 0x15), gyroscope +-256 dps at 224 Hz (CTRL3 0x45), both on (CTRL7 bits 0-1). Data: AX_L at
// 0x35, then AY, AZ, GX, GY, GZ, int16 each. ──

static void imuInit() {
    sImu = i2cDevice(kQmi8658);
    uint8_t who = 0;
    if (!sImu || !i2cRead(sImu, 0x00, &who, 1) || who != 0x05) {
        if (sImu) ESP_LOGW(TAG, "QMI8658 WHO_AM_I 0x%02X, not 0x05: not used", who);
        sImu = nullptr;
        return;
    }
    i2cWrite(sImu, 0x60, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(20));
    i2cWrite(sImu, 0x02, 0x40);
    i2cWrite(sImu, 0x03, 0x15);
    i2cWrite(sImu, 0x04, 0x45);
    i2cWrite(sImu, 0x08, 0x03);
}

// The chip's axes to the screen's (Android's: x right, y up the screen, z out of the glass).
// Measured on the v1 board with `imu` on the console (2026-10-19): flat and face up, chip z
// read -9.9; upright, chip x +10.4; right edge down, chip y -9.3. So the chip's x runs up
// the screen, its y to the right, and its z into the glass.
static constexpr int kAxisX = 1, kAxisY = 0, kAxisZ = 2;          // which chip axis
static constexpr float kSignX = 1, kSignY = 1, kSignZ = -1;       // and its sign

Motion readMotion() {
    Motion m;
    uint8_t d[12] = {};
    if (!sImu || !i2cRead(sImu, 0x35, d, sizeof d)) return m;
    float a[3], g[3];
    constexpr float kG = 9.80665f / 8192.0f;                       // +-4 g: 8192 LSB/g
    constexpr float kRad = 3.14159265f / 180.0f / 128.0f;          // +-256 dps: 128 LSB/dps
    for (int i = 0; i < 3; i++) {
        a[i] = (int16_t) (d[2 * i] | d[2 * i + 1] << 8) * kG;
        g[i] = (int16_t) (d[6 + 2 * i] | d[6 + 2 * i + 1] << 8) * kRad;
    }
    m.valid = true;
    m.ax = kSignX * a[kAxisX]; m.ay = kSignY * a[kAxisY]; m.az = kSignZ * a[kAxisZ];
    m.gx = kSignX * g[kAxisX]; m.gy = kSignY * g[kAxisY]; m.gz = kSignZ * g[kAxisZ];
    return m;
}

// Which board: Waveshare's own code tells v2 by a CST816 answering at 0x15.
static void detect() {
    sTouch = i2cDevice(kCst816);
    sV2 = sTouch != nullptr;
    if (!sTouch) sTouch = i2cDevice(kFt3168);
    sXGap = sV2 ? 0x10 : 0;
    sPmu = i2cDevice(kAxp2101);
    if (sPmu) i2cWrite(sPmu, 0x30, pmuRead(0x30) | 0x0D);   // ADCs: battery, VBUS, VSYS
    imuInit();
    ESP_LOGI(TAG, "%s: touch %s, PMU %s, IMU %s", sV2 ? "v2 (CO5300)" : "v1 (SH8601)",
             sTouch ? (sV2 ? "CST816" : "FT3168") : "none", sPmu ? "AXP2101" : "none",
             sImu ? "QMI8658" : "none");
}

// ── Panel ──
//
// Both controllers take QSPI the same way: a command is one byte on one line, wrapped as
// 0x02 | cmd << 8; pixels go on four lines after 0x32 | 0x2C << 8 (RAMWR).

static bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(sFree, &woken);
    return woken == pdTRUE;
}

static void cmd(uint8_t c, std::initializer_list<uint8_t> data = {}) {
    esp_lcd_panel_io_tx_param(sIo, (0x02 << 24) | (c << 8), data.size() ? data.begin() : nullptr,
                              data.size());
}

static void window(int x0, int y0, int x1, int y1) {
    x0 += sXGap;
    x1 += sXGap;
    cmd(0x2A, {(uint8_t) (x0 >> 8), (uint8_t) x0, (uint8_t) (x1 >> 8), (uint8_t) x1});
    cmd(0x2B, {(uint8_t) (y0 >> 8), (uint8_t) y0, (uint8_t) (y1 >> 8), (uint8_t) y1});
}

static void pixels(const uint16_t* data, size_t bytes) {
    esp_lcd_panel_io_tx_color(sIo, (0x32 << 24) | (0x2C << 8), data, bytes);
}

// AMOLED: no backlight; 0x51 sets the panel's brightness.
void setBrightness(int percent) {
    percent = std::max(0, std::min(100, percent));
    if (sIo) cmd(0x51, {(uint8_t) (percent * 255 / 100)});
}

static void fillBlack() {
    memset(sStrip[0], 0, kStripBytes);
    for (int y = 0; y < kHeight; y += kStripLines) {
        xSemaphoreTake(sFree, portMAX_DELAY);
        const int y1 = std::min(kHeight, y + kStripLines) - 1;
        window(0, y, kWidth - 1, y1);
        pixels(sStrip[0], (size_t) (y1 - y + 1) * kWidth * 2);
    }
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreGive(sFree);
    xSemaphoreGive(sFree);
}

bool displayInit() {
    detect();

    spi_bus_config_t bus = {};
    bus.sclk_io_num = kLcdClk;
    bus.data0_io_num = kLcdD0;
    bus.data1_io_num = kLcdD1;
    bus.data2_io_num = kLcdD2;
    bus.data3_io_num = kLcdD3;
    bus.max_transfer_sz = kStripBytes;
    bus.flags = SPICOMMON_BUSFLAG_QUAD;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    sFree = xSemaphoreCreateCounting(2, 2);
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = kLcdCs;
    io.dc_gpio_num = GPIO_NUM_NC;   // QSPI: the opcode, not a D/C line, says command or data
    io.spi_mode = 0;
    io.pclk_hz = kLcdHz;
    io.trans_queue_depth = 4;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    io.on_color_trans_done = onColorDone;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI2_HOST, &io, &sIo) != ESP_OK) return false;

    for (auto& s : sStrip) {
        s = (uint16_t*) heap_caps_malloc(kStripBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s) return false;
    }

    // No reset line: a software reset, then Waveshare's init (13_display_colorbar).
    cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0xFE, {0x00});
    cmd(0xC4, {0x80});
    cmd(0x3A, {0x55});   // RGB565
    cmd(0x35, {0x00});   // tearing effect on
    cmd(0x53, {0x20});   // brightness control on
    cmd(0x51, {0x00});   // dark until the first frame is in
    cmd(0x63, {0xFF});
    cmd(0x11);           // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    fillBlack();
    cmd(0x29);           // display on
    setBrightness(100);
    ESP_LOGI(TAG, "panel up: %dx%d at %d MHz QSPI", kWidth, kHeight, kLcdHz / 1000000);
    return true;
}

// Premultiplied, opaque ARGB8888 -> RGB565, byte-swapped for the panel.
static inline uint16_t toPanel565(uint32_t p) {
    const uint16_t v = (uint16_t) (((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
    return (uint16_t) ((v << 8) | (v >> 8));
}

static void sendFrame(const uint32_t* argb, int srcW, int srcH) {
    const int k = std::max(1, kWidth / std::max(1, srcW));   // integer upscale factor
    int which = 0;
    for (int y0 = 0; y0 < kHeight; y0 += kStripLines) {
        xSemaphoreTake(sFree, portMAX_DELAY);
        uint16_t* dst = sStrip[which];
        const int lines = std::min(kStripLines, kHeight - y0);
        for (int y = 0; y < lines; y++) {
            const uint32_t* src = argb + (size_t) std::min(srcH - 1, (y0 + y) / k) * srcW;
            uint16_t* d = dst + (size_t) y * kWidth;
            if (k == 1) {
                for (int x = 0; x < kWidth; x++) d[x] = toPanel565(src[x]);
            } else {
                for (int x = 0; x < kWidth; x++) d[x] = toPanel565(src[x / k]);
            }
        }
        window(0, y0, kWidth - 1, y0 + lines - 1);
        pixels(dst, (size_t) lines * kWidth * 2);
        which ^= 1;
    }
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreGive(sFree);
    xSemaphoreGive(sFree);
}

// A sender task on core 0 converts and sends frame N while the player (core 1) renders N+1.
static TaskHandle_t sSender;
static SemaphoreHandle_t sJob, sIdle;
static const uint32_t* sJobArgb;
static int sJobW, sJobH;

static void senderTask(void*) {
    while (true) {
        xSemaphoreTake(sJob, portMAX_DELAY);
        sendFrame(sJobArgb, sJobW, sJobH);
        xSemaphoreGive(sIdle);
    }
}

void presentWait() {
    if (!sIdle) return;
    xSemaphoreTake(sIdle, portMAX_DELAY);
    xSemaphoreGive(sIdle);
}

void presentAsync(const uint32_t* argb, int srcW, int srcH) {
    if (!sSender) {
        sJob = xSemaphoreCreateBinary();
        sIdle = xSemaphoreCreateBinary();
        xSemaphoreGive(sIdle);
        xTaskCreatePinnedToCore(senderTask, "lcd_send", 3072, nullptr, 6, &sSender, 0);
    }
    xSemaphoreTake(sIdle, portMAX_DELAY);
    sJobArgb = argb;
    sJobW = srcW;
    sJobH = srcH;
    sLast = argb;
    sLastW = srcW;
    sLastH = srcH;
    xSemaphoreGive(sJob);
}

int64_t present(const uint32_t* argb, int srcW, int srcH) {
    int64_t t0 = esp_timer_get_time();
    presentAsync(argb, srcW, srcH);
    presentWait();
    return esp_timer_get_time() - t0;
}

const uint16_t* frameBuffer() { return nullptr; }

void dumpFrame(int step) {
    presentWait();
    if (!sLast) { printf("SNAP BEGIN 0 0\nSNAP END\n"); return; }
    const int k = std::max(1, kWidth / std::max(1, sLastW));
    const int ow = kWidth / step, oh = kHeight / step;
    static uint16_t row[kWidth];
    static unsigned char b64[kWidth * 3];
    printf("\nSNAP BEGIN %d %d\n", ow, oh);
    for (int y = 0; y < oh; y++) {
        const uint32_t* src = sLast + (size_t) std::min(sLastH - 1, y * step / k) * sLastW;
        for (int x = 0; x < ow; x++) {
            const uint32_t p = src[std::min(sLastW - 1, x * step / k)];
            row[x] = (uint16_t) (((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
        }
        size_t olen = 0;
        mbedtls_base64_encode(b64, sizeof(b64), &olen, (const unsigned char*) row, ow * 2);
        b64[olen] = 0;
        printf("%s\n", b64);
        if ((y & 15) == 15) fflush(stdout);
    }
    printf("SNAP END\n");
    fflush(stdout);
}

// ── Touch: FT3168 and CST816 both keep the finger count at 0x02 and X, Y (12 bits each, the
// top bits of the high bytes are event flags) at 0x03..0x06 ──

static bool rawTouch(int& x, int& y) {
    uint8_t d[5] = {};
    if (!i2cRead(sTouch, 0x02, d, sizeof d) || (d[0] & 0x0F) == 0) return false;
    x = ((d[1] & 0x0F) << 8) | d[2];
    y = ((d[3] & 0x0F) << 8) | d[4];
    return true;
}

// The FT3168 can sleep through the probe at boot (it answered only once tapped, seen on a
// v1 board): look again once a second until it answers. v1/v2 was settled at boot by the
// CST816, which doesn't do this.
static bool touchPresent() {
    static int64_t next = 0;
    if (sTouch) return true;
    const int64_t now = esp_timer_get_time();
    if (now < next) return false;
    next = now + 1000000;
    sTouch = i2cDevice(sV2 ? kCst816 : kFt3168);
    if (sTouch) ESP_LOGI(TAG, "touch %s answered", sV2 ? "CST816" : "FT3168");
    return sTouch != nullptr;
}

Touch readTouch() {
    Touch t;
    int x, y;
    if (!touchPresent() || !rawTouch(x, y) || x >= kWidth || y >= kHeight) return t;
    t.down = true;
    t.x = x;
    t.y = y;
    return t;
}

static void touchProbe() {
    for (int i = 0; i < 3 && !sTouch; i++) {
        touchPresent();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!sTouch) { printf("touch: no controller (tap the screen to wake it, then try again)\n"); return; }
    printf("touch: raw coordinates for 15 s\n");
    int lastX = -1, lastY = -1;
    for (int64_t end = esp_timer_get_time() + 15000000; esp_timer_get_time() < end;) {
        int x, y;
        if (rawTouch(x, y) && (std::abs(x - lastX) > 3 || std::abs(y - lastY) > 3)) {
            printf("touch raw x=%d y=%d\n", x, y);
            lastX = x;
            lastY = y;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("touch: done\n");
}

void consoleInit() {
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 1024;
    cfg.tx_buffer_size = 4096;
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, nullptr, _IONBF, 0);
}

void radioPower() {}

// ── Buttons: the PWR key belongs to the AXP2101, which latches a short press in IRQ status 2
// (0x49 bit 3, as on the Core2 v1.1); BOOT is GPIO0, low while held. PWR steps forward, BOOT
// back. ──

int buttonPressed() {
    static int64_t next = 0;
    static bool bootInit = false, bootWas = false;
    const int64_t now = esp_timer_get_time();
    if (now < next) return 0;
    next = now + 100000;
    if (!bootInit) {
        gpio_config_t in = {};
        in.pin_bit_mask = 1ULL << kBoot;
        in.mode = GPIO_MODE_INPUT;
        in.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&in);
        bootInit = true;
    }
    const bool boot = gpio_get_level(kBoot) == 0;
    const bool bootPress = boot && !bootWas;
    bootWas = boot;
    bool pwrPress = false;
    if (sPmu) {
        const uint8_t v = pmuRead(0x49) & 0x0C;
        if (v) i2cWrite(sPmu, 0x49, v);
        pwrPress = v & 0x08;
    }
    return pwrPress ? 1 : bootPress ? -1 : 0;
}

// ── Power: the AXP2101 (Core2 v1.1's PMU): battery mV at 0x34 (14 bits), fuel gauge at 0xA4 ──

void powerInit() {}   // detect() ran from displayInit()

static int levelFromMillivolts(float mv) {
    return (int) std::lround(std::fmax(0.0, std::fmin(100.0, (mv - 3300) * 100 / 850)));
}

Power readPower() {
    Power p;
    if (!sPmu) return p;
    const uint8_t s0 = pmuRead(0x00), s1 = pmuRead(0x01);
    const bool battery = s0 & 0x08;
    const bool external = s0 & 0x20;       // VBUS good
    const int dir = (s1 >> 5) & 3;         // 1 charging, 2 discharging, 0 standby
    p.valid = true;
    p.charging = dir == 1;
    // Without a cell the voltage ADC reads full scale (16.38 V): report it only with one.
    p.batteryVolts = battery ? (((pmuRead(0x34) & 0x3F) << 8) | pmuRead(0x35)) * 1e-3f : 0;
    const uint8_t gauge = pmuRead(0xA4);
    p.batteryLevel = battery ? (gauge <= 100 ? gauge : levelFromMillivolts(p.batteryVolts * 1000)) : -1;
    p.powerState = !battery ? 2 : p.charging ? 1 : external ? 2 : 0;
    return p;
}

bool console(const std::string& line) {
    if (line == "touch") { touchProbe(); return true; }
    if (line == "i2c") {
        printf("i2c (SDA 15, SCL 14):");
        i2cDevice(kAxp2101);   // brings the bus up
        for (int a = 0x08; a < 0x78; a++)
            if (sBus && i2c_master_probe(sBus, a, 20) == ESP_OK) printf(" 0x%02X", a);
        printf("\n%s\n", sV2 ? "v2: CO5300 + CST816" : "v1: SH8601 + FT3168");
        return true;
    }
    if (line == "imu") {
        if (!sImu) { printf("imu: no QMI8658\n"); return true; }
        printf("imu: screen axes, m/s2 and rad/s, for 10 s\n");
        for (int i = 0; i < 20; i++) {
            Motion m = readMotion();
            printf("imu a %6.2f %6.2f %6.2f  g %6.2f %6.2f %6.2f\n", m.ax, m.ay, m.az, m.gx, m.gy, m.gz);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        return true;
    }
    if (line == "power") {
        Power p = readPower();
        printf("battery %.2f V, level %d %%, state %d, charging %d\n", p.batteryVolts,
               p.batteryLevel, p.powerState, p.charging);
        return true;
    }
    return false;
}

// Not on this board (yet): no RTC driver, nothing to beep with, a panel that needs no refresh.
bool rtcRead(int64_t*) { return false; }
bool rtcWrite(int64_t) { return false; }
bool beep(int, int) { return false; }
bool displayRefresh() { return false; }

const char* wakeReason() { return "power_on"; }
bool sleepFor(int, Sleep) { return false; }

}  // namespace board
