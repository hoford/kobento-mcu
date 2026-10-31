// Waveshare ESP32-S3 1.28" round: display (GC9A01A on SPI), touch (CST816S on I2C, touch
// variant only), backlight (LEDC PWM), battery (ADC through a 1/3 divider), console (UART0).
//
// Pins are from the Waveshare wiki pages ESP32-S3-LCD-1.28 and ESP32-S3-Touch-LCD-1.28; the
// panel init is the sequence in Waveshare's LCD_1in28 driver (the same one the GC9A01
// drivers in TFT_eSPI and esp_lcd_gc9a01 carry).
#include "board/board.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"

static const char* TAG = "round";

namespace board {

// Shared by both variants.
static constexpr gpio_num_t kSclk = GPIO_NUM_10, kMosi = GPIO_NUM_11, kLcdCs = GPIO_NUM_9,
                            kLcdDc = GPIO_NUM_8, kSda = GPIO_NUM_6, kScl = GPIO_NUM_7,
                            kTouchRst = GPIO_NUM_13, kTouchInt = GPIO_NUM_5;
// Different per variant: LCD reset and backlight.
struct Variant { const char* name; gpio_num_t lcdRst, backlight; };
static constexpr Variant kTouchBoard = {"ESP32-S3-Touch-LCD-1.28", GPIO_NUM_14, GPIO_NUM_2};
static constexpr Variant kPlainBoard = {"ESP32-S3-LCD-1.28", GPIO_NUM_12, GPIO_NUM_40};
static Variant sVariant = kPlainBoard;

static constexpr int kLcdHz = 40 * 1000 * 1000;
static constexpr int kStripLines = 24;
static constexpr size_t kStripBytes = (size_t) kWidth * kStripLines * 2;

static esp_lcd_panel_io_handle_t sIo;
static uint16_t* sStrip[2];
static SemaphoreHandle_t sFree;   // counts strip buffers not on the wire
static const uint32_t* sLast;     // last presented frame, for dumpFrame
static int sLastW, sLastH;
static i2c_master_bus_handle_t sBus;
static i2c_master_dev_handle_t sTouch;

// ── I2C (touch 0x15, IMU 0x6B) ──

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

static void output(gpio_num_t pin, int level) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, level);
}

// The touch variant has a CST816S at 0x15 behind a reset on GPIO13; it answers only after a
// reset (it sleeps otherwise), so reset it, then look.
static void detectVariant() {
    output(kTouchRst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(kTouchRst, 1);
    vTaskDelay(pdMS_TO_TICKS(60));
    sTouch = i2cDevice(0x15);
    sVariant = sTouch ? kTouchBoard : kPlainBoard;
    if (sTouch) {
        i2cWrite(sTouch, 0xFE, 0x01);   // DisAutoSleep: stay awake, so polling works
        gpio_config_t in = {};
        in.pin_bit_mask = 1ULL << kTouchInt;
        in.mode = GPIO_MODE_INPUT;
        in.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&in);
    }
    ESP_LOGI(TAG, "%s (touch %s)", sVariant.name, sTouch ? "CST816S" : "none");
}

// ── Panel ──

static bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(sFree, &woken);
    return woken == pdTRUE;
}

static void cmd(uint8_t c, std::initializer_list<uint8_t> data = {}) {
    esp_lcd_panel_io_tx_param(sIo, c, data.size() ? data.begin() : nullptr, data.size());
}

static void backlightInit() {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = 20000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&t);
    ledc_channel_config_t c = {};
    c.gpio_num = sVariant.backlight;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_0;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    ledc_channel_config(&c);
}

void setBrightness(int percent) {
    percent = std::max(0, std::min(100, percent));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void fillBlack() {
    memset(sStrip[0], 0, kStripBytes);
    cmd(0x2A, {0, 0, 0, kWidth - 1});
    for (int y = 0; y < kHeight; y += kStripLines) {
        xSemaphoreTake(sFree, portMAX_DELAY);
        const int y1 = std::min(kHeight, y + kStripLines) - 1;
        cmd(0x2B, {0, (uint8_t) y, 0, (uint8_t) y1});
        esp_lcd_panel_io_tx_color(sIo, 0x2C, sStrip[0], (size_t) (y1 - y + 1) * kWidth * 2);
    }
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreGive(sFree);
    xSemaphoreGive(sFree);
}

bool displayInit() {
    detectVariant();
    backlightInit();

    output(kLcdCs, 1);
    output(sVariant.lcdRst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(sVariant.lcdRst, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t bus = {};
    bus.mosi_io_num = kMosi;
    bus.miso_io_num = -1;
    bus.sclk_io_num = kSclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kStripBytes;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    sFree = xSemaphoreCreateCounting(2, 2);
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = kLcdCs;
    io.dc_gpio_num = kLcdDc;
    io.spi_mode = 0;
    io.pclk_hz = kLcdHz;
    io.trans_queue_depth = 4;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.on_color_trans_done = onColorDone;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI2_HOST, &io, &sIo) != ESP_OK) return false;

    for (auto& s : sStrip) {
        s = (uint16_t*) heap_caps_malloc(kStripBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s) return false;
    }

    // GC9A01A, Waveshare's LCD_1in28 init: unlock the inter registers, then power, gamma
    // and timing. MADCTL 0x08 (BGR) is Waveshare's upright orientation, USB at the bottom.
    cmd(0xEF);
    cmd(0xEB, {0x14});
    cmd(0xFE);
    cmd(0xEF);
    cmd(0xEB, {0x14});
    cmd(0x84, {0x40});
    cmd(0x85, {0xFF});
    cmd(0x86, {0xFF});
    cmd(0x87, {0xFF});
    cmd(0x88, {0x0A});
    cmd(0x89, {0x21});
    cmd(0x8A, {0x00});
    cmd(0x8B, {0x80});
    cmd(0x8C, {0x01});
    cmd(0x8D, {0x01});
    cmd(0x8E, {0xFF});
    cmd(0x8F, {0xFF});
    cmd(0xB6, {0x00, 0x20});
    cmd(0x36, {0x08});
    cmd(0x3A, {0x05});   // RGB565
    cmd(0x90, {0x08, 0x08, 0x08, 0x08});
    cmd(0xBD, {0x06});
    cmd(0xBC, {0x00});
    cmd(0xFF, {0x60, 0x01, 0x04});
    cmd(0xC3, {0x13});
    cmd(0xC4, {0x13});
    cmd(0xC9, {0x22});
    cmd(0xBE, {0x11});
    cmd(0xE1, {0x10, 0x0E});
    cmd(0xDF, {0x21, 0x0C, 0x02});
    cmd(0xF0, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A});
    cmd(0xF1, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F});
    cmd(0xF2, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A});
    cmd(0xF3, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F});
    cmd(0xED, {0x1B, 0x0B});
    cmd(0xAE, {0x77});
    cmd(0xCD, {0x63});
    cmd(0x70, {0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03});
    cmd(0xE8, {0x34});
    cmd(0x62, {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70});
    cmd(0x63, {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70});
    cmd(0x64, {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07});
    cmd(0x66, {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00});
    cmd(0x67, {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98});
    cmd(0x74, {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00});
    cmd(0x98, {0x3E, 0x07});
    cmd(0x35);   // TEON
    cmd(0x21);   // INVON
    cmd(0x11);   // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x29);   // DISPON
    vTaskDelay(pdMS_TO_TICKS(20));

    fillBlack();
    setBrightness(100);
    ESP_LOGI(TAG, "panel up: %dx%d GC9A01A at %d MHz, backlight GPIO%d", kWidth, kHeight,
             kLcdHz / 1000000, (int) sVariant.backlight);
    return true;
}

// Premultiplied, opaque ARGB8888 -> RGB565, byte-swapped for the panel.
static inline uint16_t toPanel565(uint32_t p) {
    const uint16_t v = (uint16_t) (((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
    return (uint16_t) ((v << 8) | (v >> 8));
}

static void sendFrame(const uint32_t* argb, int srcW, int srcH) {
    const int k = std::max(1, kWidth / std::max(1, srcW));   // integer upscale factor
    cmd(0x2A, {0, 0, 0, kWidth - 1});
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
        const int y1 = y0 + lines - 1;
        cmd(0x2B, {0, (uint8_t) y0, 0, (uint8_t) y1});
        esp_lcd_panel_io_tx_color(sIo, 0x2C, dst, (size_t) lines * kWidth * 2);
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

// ── Touch: CST816S, reg 0x02 = finger count, 0x03..0x06 = X and Y (12 bits each) ──

Touch readTouch() {
    Touch t;
    if (!sTouch) return t;
    uint8_t d[5] = {};
    if (!i2cRead(sTouch, 0x02, d, sizeof d) || (d[0] & 0x0F) == 0) return t;
    const int x = ((d[1] & 0x0F) << 8) | d[2];
    const int y = ((d[3] & 0x0F) << 8) | d[4];
    if (x >= kWidth || y >= kHeight) return t;
    t.down = true;
    t.x = x;
    t.y = y;
    return t;
}

static void touchProbe() {
    if (!sTouch) { printf("touch: none (this is the %s)\n", sVariant.name); return; }
    printf("touch: raw coordinates for 15 s\n");
    int lastX = -1, lastY = -1;
    for (int64_t end = esp_timer_get_time() + 15000000; esp_timer_get_time() < end;) {
        uint8_t d[5] = {};
        if (i2cRead(sTouch, 0x02, d, sizeof d) && (d[0] & 0x0F)) {
            const int x = ((d[1] & 0x0F) << 8) | d[2], y = ((d[3] & 0x0F) << 8) | d[4];
            if (std::abs(x - lastX) > 3 || std::abs(y - lastY) > 3) {
                printf("touch raw x=%d y=%d\n", x, y);
                lastX = x;
                lastY = y;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("touch: done\n");
}

void consoleInit() {
    uart_driver_install(UART_NUM_0, 1024, 0, 0, nullptr, 0);
    uart_vfs_dev_use_driver(UART_NUM_0);
    setvbuf(stdin, nullptr, _IONBF, 0);
}

void radioPower() {}

// Only reset and boot: nothing to read.
int buttonPressed() { return 0; }

// The touch variant's QMI8658 (0x6B) isn't read yet.
Motion readMotion() { return {}; }

// ── Power: the battery through a 200k/100k divider on GPIO1 (ADC1 channel 0). There is no
// charge-status line, so the state is unknown; the level comes from the voltage. ──

static adc_oneshot_unit_handle_t sAdc;
static adc_cali_handle_t sCali;

void powerInit() {
    adc_oneshot_unit_init_cfg_t u = {};
    u.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&u, &sAdc) != ESP_OK) { sAdc = nullptr; return; }
    adc_oneshot_chan_cfg_t c = {};
    c.atten = ADC_ATTEN_DB_12;
    c.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(sAdc, ADC_CHANNEL_0, &c);
    adc_cali_curve_fitting_config_t cc = {};
    cc.unit_id = ADC_UNIT_1;
    cc.chan = ADC_CHANNEL_0;
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cc, &sCali) != ESP_OK) sCali = nullptr;
}

Power readPower() {
    Power p;
    if (!sAdc) return p;
    int mvSum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0, mv = 0;
        adc_oneshot_read(sAdc, ADC_CHANNEL_0, &raw);
        if (sCali) adc_cali_raw_to_voltage(sCali, raw, &mv);
        else mv = raw * 3300 / 4095;
        mvSum += mv;
    }
    const float volts = mvSum / 8.0f * 3.0f / 1000.0f;
    p.valid = true;
    p.batteryVolts = volts;
    // With no cell the pin reads the charger's output (4.6 V seen on USB) or close to
    // nothing; no Li-ion cell is above 4.4 V or below 2.8 V, so both mean "no battery".
    if (volts < 2.8f || volts > 4.4f) {
        p.batteryLevel = -1;
        p.powerState = 2;
    } else {
        p.batteryLevel = (int) std::lround(std::fmax(0.0, std::fmin(100.0, (volts * 1000 - 3300) * 100 / 850)));
        p.powerState = -1;
    }
    return p;
}

bool console(const std::string& line) {
    if (line == "touch") { touchProbe(); return true; }
    if (line == "i2c") {
        printf("i2c (SDA 6, SCL 7):");
        i2cDevice(0x15);   // brings the bus up
        for (int a = 0x08; a < 0x78; a++)
            if (sBus && i2c_master_probe(sBus, a, 20) == ESP_OK) printf(" 0x%02X", a);
        printf("\n%s\n", sVariant.name);
        return true;
    }
    if (line == "power") {
        Power p = readPower();
        printf("battery %.2f V, level %d %%\n", p.batteryVolts, p.batteryLevel);
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
