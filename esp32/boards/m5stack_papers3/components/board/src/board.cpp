// M5Stack PaperS3: display (ED047TC1 e-ink on an 8-bit parallel bus), touch (GT911 on I2C),
// battery (ADC through a 1/2 divider, charge-status and USB-detect lines), console (USB
// Serial/JTAG).
//
// The panel has no controller: the ESP32 drives the source and gate drivers itself, one
// frame (all 540 lines) at a time, and a picture takes a sequence of frames, each pushing
// every pixel toward black, toward white or not at all. The pins, the bus timing, the
// power sequence and the waveforms (the LUT rows below) are from M5Stack's M5GFX
// (src/lgfx/v1/platforms/esp32/Bus_EPD.inl and Panel_EPD.inl, MIT / FreeBSD licences,
// by lovyan03); the rest is written for this player.
#include "board/board.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

#include "driver/gpio.h"
#include "bmi270.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/gpio_ll.h"
#include "mbedtls/base64.h"
#include "soc/gpio_sig_map.h"

static const char* TAG = "paper";

namespace board {

// ── Pins (M5GFX's PaperS3 configuration) ──
static constexpr gpio_num_t kData[8] = {GPIO_NUM_6, GPIO_NUM_14, GPIO_NUM_7, GPIO_NUM_12,
                                        GPIO_NUM_9, GPIO_NUM_11, GPIO_NUM_8, GPIO_NUM_10};
static constexpr gpio_num_t kPwr = GPIO_NUM_46,   // panel power (the TPS65185-style rails)
                            kSpv = GPIO_NUM_17,   // gate start pulse (STV)
                            kCkv = GPIO_NUM_18,   // gate clock
                            kSph = GPIO_NUM_13,   // source start pulse (XSTL), as the i80 CS
                            kOe = GPIO_NUM_45,    // source output enable
                            kLe = GPIO_NUM_15,    // source latch enable
                            kCl = GPIO_NUM_16;    // source clock (XCL), as the i80 WR
static constexpr gpio_num_t kSda = GPIO_NUM_41, kScl = GPIO_NUM_42, kTouchInt = GPIO_NUM_48;
static constexpr gpio_num_t kPowerOff = GPIO_NUM_44;   // pulsed to switch the board off
static constexpr gpio_num_t kChargeStat = GPIO_NUM_4;  // low while charging
static constexpr adc_channel_t kBatChannel = ADC_CHANNEL_2;   // GPIO3, through 1/2
static constexpr adc_channel_t kUsbChannel = ADC_CHANNEL_4;   // GPIO5: > 0.2 V on USB

// ── The panel's own geometry: 960 columns per line, 540 lines, 2 bits per pixel on the bus.
// Portrait pixel (x, y) is panel column y, line 539 - x. ──
static constexpr int kCols = 960, kLines = 540;
static constexpr int kLineBytes = kCols / 4 + 8;   // 8 bytes of padding, as M5GFX sends
static constexpr int kBusHz = 16 * 1000 * 1000;

static inline void hi(gpio_num_t p) { gpio_ll_set_level(&GPIO, p, 1); }
static inline void lo(gpio_num_t p) { gpio_ll_set_level(&GPIO, p, 0); }

static void output(gpio_num_t pin, int level) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, level);
}

// ── Waveforms. Each row is one frame: 16 two-bit codes, one per grey level (0 = black,
// 15 = white): 1 drives toward black, 2 toward white, 3 leaves the pixel alone. ──
#define LUT(d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, da, db, dc, dd, de, df)                     \
    (uint32_t) ((d0 << 0) | (d1 << 2) | (d2 << 4) | (d3 << 6) | (d4 << 8) | (d5 << 10) |        \
                (d6 << 12) | (d7 << 14) | (d8 << 16) | (d9 << 18) | (da << 20) | (db << 22) |   \
                (dc << 24) | (dd << 26) | (de << 28) | ((uint32_t) df << 30))
// Indexed by a pixel's old level: moves it to about mid-grey before the new picture.
static constexpr uint32_t kEraser[] = {
    LUT(2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 1, 1),
    LUT(2, 2, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1),
    ~0u,
};
// Indexed by the new level: 16 greys.
static constexpr uint32_t kQuality[] = {
    LUT(1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 2, 1, 1, 1, 1, 1),
    LUT(2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2),
    LUT(2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2),
    LUT(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1),
    LUT(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1),
    LUT(1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1),
    LUT(1, 1, 2, 2, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 3),
    LUT(1, 1, 1, 1, 1, 2, 1, 1, 2, 2, 1, 2, 1, 2, 2, 2),
    LUT(1, 1, 3, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1, 1, 2, 2),
    LUT(3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2),
    LUT(3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2),
    LUT(1, 1, 1, 1, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2),
    LUT(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 2, 2, 2, 2),
    LUT(3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3),
    LUT(3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 3),
};
// Indexed by the new level, from the old picture directly: black and white only.
static constexpr uint32_t kFast[] = {
    LUT(2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1),
    LUT(2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1),
    LUT(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
    LUT(1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2),
};
#undef LUT

static constexpr uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

enum class Mode { Quality, Fast };
static std::atomic<Mode> sMode{Mode::Quality};
static std::atomic<bool> sForce{true};   // the first picture redraws every pixel

// ── Bus: the LCD peripheral's i80 mode clocks one line's bytes out on CL with DMA; CKV, LE
// and SPV are plain GPIOs around it. ──

static esp_lcd_i80_bus_handle_t sI80;
static esp_lcd_panel_io_handle_t sIo;
static volatile bool sLineBusy;
static uint8_t* sLineBuf[2];   // DMA
static uint8_t* sNoopLine;     // DMA: every pixel "leave alone"

static bool IRAM_ATTR onLineDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    lo(kCkv);
    hi(kLe);
    sLineBusy = false;
    return false;
}

static inline void waitLine() {
    while (sLineBusy) {}
}

static void sendLine(const uint8_t* data) {
    waitLine();
    sLineBusy = true;
    lo(kLe);
    hi(kCkv);
    esp_lcd_panel_io_tx_color(sIo, -1, data, kLineBytes);
}

static void frameBegin() {
    waitLine();
    lo(kSpv);
    esp_rom_delay_us(1);
    lo(kCkv);
    esp_rom_delay_us(3);
    hi(kCkv);
    esp_rom_delay_us(1);
    hi(kSpv);
    for (int i = 0; i < 3; i++) {
        esp_rom_delay_us(3);
        lo(kCkv);
        esp_rom_delay_us(3);
        hi(kCkv);
    }
}

static void frameEnd() {
    waitLine();
    lo(kLe);
    hi(kCkv);
}

static void panelPower(bool on) {
    waitLine();
    if (on) {
        hi(kOe);
        esp_rom_delay_us(100);
        hi(kPwr);
        esp_rom_delay_us(100);
        hi(kSpv);
        vTaskDelay(pdMS_TO_TICKS(1));
    } else {
        vTaskDelay(pdMS_TO_TICKS(1));
        lo(kPwr);
        esp_rom_delay_us(10);
        lo(kOe);
        esp_rom_delay_us(100);
        lo(kSpv);
    }
}

static bool busInit() {
    for (gpio_num_t p : {kSpv, kCkv, kSph, kOe, kLe, kCl}) output(p, 0);
    // From IDF 5.4 the i80 driver leaves the data pins' direction alone, and some (11, 12)
    // come up open-drain: make them plain outputs first.
    for (gpio_num_t p : kData) output(p, 0);

    esp_lcd_i80_bus_config_t bus = {};
    bus.clk_src = LCD_CLK_SRC_PLL160M;
    bus.dc_gpio_num = kPwr;   // the driver insists on a D/C pin; it is taken back below
    bus.wr_gpio_num = kCl;
    bus.bus_width = 8;
    for (int i = 0; i < 8; i++) bus.data_gpio_nums[i] = kData[i];
    bus.max_transfer_bytes = 1024;
    if (esp_lcd_new_i80_bus(&bus, &sI80) != ESP_OK) return false;
    // PWR back to a plain GPIO: nothing in a line transfer needs D/C.
    esp_rom_gpio_connect_out_signal(kPwr, SIG_GPIO_OUT_IDX, false, false);
    output(kPwr, 0);

    esp_lcd_panel_io_i80_config_t io = {};
    io.cs_gpio_num = kSph;
    io.pclk_hz = kBusHz;
    io.trans_queue_depth = 4;
    io.on_color_trans_done = onLineDone;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.flags.pclk_idle_low = 1;
    if (esp_lcd_new_panel_io_i80(sI80, &io, &sIo) != ESP_OK) return false;

    for (auto& b : sLineBuf) {
        b = (uint8_t*) heap_caps_calloc(1, kLineBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!b) return false;
    }
    sNoopLine = (uint8_t*) heap_caps_calloc(1, kLineBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!sNoopLine) return false;
    memset(sNoopLine, 0xFF, kCols / 4);
    return true;
}

// ── Pictures: one byte per pixel (grey 0..15) in panel order (line, column). ──
// sPending: the newest frame from the player, under sLock. sTarget: what the update task is
// drawing. sShown: what the panel holds.
static uint8_t *sPending, *sTarget, *sShown;
static bool sHavePending;
static SemaphoreHandle_t sLock, sKick;
static bool sLineDirty[kLines];
static std::atomic<int> sUpdates{0}, sLastMs{0}, sLastChanged{0};
static std::atomic<bool> sUpdating{false};   // a picture is going to the panel

// One line's 2-bit codes for one waveform row. `byOld` indexes the row by the shown level
// (the eraser), otherwise by the new one; pixels that don't change are left alone. `bw`
// (the fast waveform, which only knows black and white) rounds the new level to one of them.
static void buildLine(uint8_t* out, int line, uint32_t row, bool byOld, bool force, bool bw) {
    uint8_t code[16];
    for (int l = 0; l < 16; l++) code[l] = (row >> (2 * (bw ? (l < 8 ? 0 : 15) : l))) & 3;
    const uint8_t* o = sShown + (size_t) line * kCols;
    const uint8_t* n = sTarget + (size_t) line * kCols;
    const uint8_t* by = byOld ? o : n;
    for (int x = 0; x < kCols; x += 4) {
        uint8_t b = 0;
        for (int k = 0; k < 4; k++) {
            const uint8_t c = (force || o[x + k] != n[x + k]) ? code[by[x + k]] : 3;
            b = (uint8_t) ((b << 2) | c);
        }
        out[x >> 2] = b;
    }
}

static void runFrame(uint32_t row, bool byOld, bool force, bool bw = false) {
    frameBegin();
    for (int y = 0; y < kLines; y++) {
        const uint8_t* data = sNoopLine;
        if (force || sLineDirty[y]) {
            uint8_t* buf = sLineBuf[y & 1];
            buildLine(buf, y, row, byOld, force, bw);   // while the line before goes out
            data = buf;
        }
        sendLine(data);
    }
    frameEnd();
    vTaskDelay(1);   // IDLE on this core, and the watchdog
}

static void updateTask(void*) {
    while (true) {
        xSemaphoreTake(sKick, portMAX_DELAY);
        xSemaphoreTake(sLock, portMAX_DELAY);
        const bool have = sHavePending;
        if (have) {
            std::swap(sPending, sTarget);
            sHavePending = false;
            sUpdating = true;
        }
        xSemaphoreGive(sLock);
        if (!have) continue;

        const bool force = sForce.exchange(false);
        int changed = 0;
        for (int y = 0; y < kLines; y++) {
            const size_t at = (size_t) y * kCols;
            int c = 0;
            for (int x = 0; x < kCols; x++) c += sShown[at + x] != sTarget[at + x];
            sLineDirty[y] = c != 0;
            changed += c;
        }
        if (!changed && !force) { sUpdating = false; continue; }

        const int64_t t0 = esp_timer_get_time();
        panelPower(true);
        if (sMode == Mode::Fast && !force) {
            for (uint32_t r : kFast) runFrame(r, false, false, true);
        } else {
            for (uint32_t r : kEraser) runFrame(r, true, force);
            for (uint32_t r : kQuality) runFrame(r, false, force);
        }
        panelPower(false);
        memcpy(sShown, sTarget, (size_t) kCols * kLines);
        sLastMs = (int) ((esp_timer_get_time() - t0) / 1000);
        sLastChanged = force ? kCols * kLines : changed;
        sUpdates++;
        sUpdating = false;
    }
}

bool displayInit() {
    output(kPowerOff, 0);   // as M5GFX leaves it; pulsing it switches the board off
    const size_t n = (size_t) kCols * kLines;
    sPending = (uint8_t*) heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    sTarget = (uint8_t*) heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    sShown = (uint8_t*) heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!sPending || !sTarget || !sShown) return false;
    memset(sShown, 8, n);   // unknown: call it mid-grey, and redraw everything first
    if (!busInit()) {
        ESP_LOGE(TAG, "e-ink bus init failed");
        return false;
    }
    sLock = xSemaphoreCreateMutex();
    sKick = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(updateTask, "epd", 4096, nullptr, 4, nullptr, 0);
    ESP_LOGI(TAG, "panel up: %dx%d e-ink (ED047TC1), bus %d MHz", kWidth, kHeight, kBusHz / 1000000);
    return true;
}

void setBrightness(int) {}

// Premultiplied, opaque ARGB8888 -> 16 greys with a 4x4 ordered dither, rotated into panel
// order. The dither depends only on the position, so an unchanged picture converts to the
// same bytes and costs no refresh.
void presentAsync(const uint32_t* argb, int srcW, int srcH) {
    if (!sLock) return;
    const int k = std::max(1, kWidth / std::max(1, srcW));
    xSemaphoreTake(sLock, portMAX_DELAY);
    for (int line = 0; line < kLines; line++) {
        const int px = std::min(srcW - 1, (kWidth - 1 - line) / k);
        uint8_t* d = sPending + (size_t) line * kCols;
        const uint8_t* bayer = &kBayer[(line & 3) << 2];
        for (int col = 0; col < kCols; col++) {
            const uint32_t p = argb[(size_t) std::min(srcH - 1, col / k) * srcW + px];
            const int lum = (int) ((((p >> 16) & 0xFF) * 77 + ((p >> 8) & 0xFF) * 151 + (p & 0xFF) * 28) >> 8);
            const int g = (lum + bayer[col & 3] - 8) >> 4;
            d[col] = (uint8_t) std::clamp(g, 0, 15);
        }
    }
    sHavePending = true;
    xSemaphoreGive(sLock);
    xSemaphoreGive(sKick);
}

void presentWait() {}

int64_t present(const uint32_t* argb, int srcW, int srcH) {
    const int64_t t0 = esp_timer_get_time();
    presentAsync(argb, srcW, srcH);
    return esp_timer_get_time() - t0;
}

const uint16_t* frameBuffer() { return nullptr; }

void dumpFrame(int step) {
    if (!sShown) { printf("SNAP BEGIN 0 0\nSNAP END\n"); return; }
    step = std::max(1, step);
    const int ow = kWidth / step, oh = kHeight / step;
    static uint16_t row[kWidth];
    static unsigned char b64[kWidth * 3];
    printf("\nSNAP BEGIN %d %d\n", ow, oh);
    for (int y = 0; y < oh; y++) {
        for (int x = 0; x < ow; x++) {
            const int g = sShown[(size_t) (kWidth - 1 - x * step) * kCols + y * step];
            const uint16_t g5 = (uint16_t) (g * 31 / 15), g6 = (uint16_t) (g * 63 / 15);
            row[x] = (uint16_t) ((g5 << 11) | (g6 << 5) | g5);
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

// ── I2C (GT911 0x5D or 0x14, BM8563 RTC 0x51, BMI270 IMU 0x68) ──

static i2c_master_bus_handle_t sBus;
static i2c_master_dev_handle_t sTouch;
static int sTouchAddr;

static bool busUp() {
    if (sBus) return true;
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = kSda;
    cfg.scl_io_num = kScl;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&cfg, &sBus) == ESP_OK;
}

static bool gtRead(uint16_t reg, uint8_t* out, size_t n) {
    const uint8_t r[2] = {(uint8_t) (reg >> 8), (uint8_t) reg};
    return sTouch && i2c_master_transmit_receive(sTouch, r, 2, out, n, 50) == ESP_OK;
}

static bool gtWrite(uint16_t reg, uint8_t v) {
    const uint8_t b[3] = {(uint8_t) (reg >> 8), (uint8_t) reg, v};
    return sTouch && i2c_master_transmit(sTouch, b, 3, 50) == ESP_OK;
}

// The GT911 has no reset line here; it answers at 0x5D or 0x14, depending on INT at power-up.
static void touchInit() {
    if (sTouch || !busUp()) return;
    gpio_config_t in = {};
    in.pin_bit_mask = 1ULL << kTouchInt;
    in.mode = GPIO_MODE_INPUT;
    gpio_config(&in);
    for (int addr : {0x5D, 0x14}) {
        if (i2c_master_probe(sBus, addr, 50) != ESP_OK) continue;
        i2c_device_config_t dev = {};
        dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev.device_address = (uint16_t) addr;
        dev.scl_speed_hz = 400000;
        if (i2c_master_bus_add_device(sBus, &dev, &sTouch) == ESP_OK) sTouchAddr = addr;
        break;
    }
    uint8_t id[4] = {}, res[4] = {};
    gtRead(0x8140, id, 4);
    gtRead(0x8048, res, 4);
    ESP_LOGI(TAG, "touch: %s at 0x%02X, id \"%.4s\", %dx%d", sTouch ? "GT911" : "none", sTouchAddr,
             (const char*) id, res[0] | res[1] << 8, res[2] | res[3] << 8);
}

// Raw GT911 point -> portrait panel pixels.
static void touchMap(int rx, int ry, int& x, int& y) {
    x = rx;
    y = ry;
}

// The GT911 sets bit 7 of its status register when a new report is ready (about every
// 10 ms) and holds it until cleared; between reports the last one stands.
Touch readTouch() {
    static Touch last;
    if (!sTouch) { touchInit(); if (!sTouch) return last; }
    uint8_t st = 0;
    if (!gtRead(0x814E, &st, 1) || !(st & 0x80)) return last;
    Touch t;
    if ((st & 0x0F) > 0) {
        uint8_t d[4] = {};
        if (gtRead(0x8150, d, 4)) {
            int x, y;
            touchMap(d[0] | d[1] << 8, d[2] | d[3] << 8, x, y);
            if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
                t.down = true;
                t.x = x;
                t.y = y;
            }
        }
    }
    gtWrite(0x814E, 0);
    last = t;
    return t;
}

static void touchProbe() {
    touchInit();
    if (!sTouch) { printf("touch: no GT911\n"); return; }
    printf("touch: raw coordinates for 15 s\n");
    int lastX = -1, lastY = -1;
    for (int64_t end = esp_timer_get_time() + 15000000; esp_timer_get_time() < end;) {
        uint8_t st = 0, d[4] = {};
        if (gtRead(0x814E, &st, 1) && (st & 0x80)) {
            if ((st & 0x0F) && gtRead(0x8150, d, 4)) {
                const int x = d[0] | d[1] << 8, y = d[2] | d[3] << 8;
                if (std::abs(x - lastX) > 3 || std::abs(y - lastY) > 3) {
                    int mx, my;
                    touchMap(x, y, mx, my);
                    printf("touch raw x=%d y=%d -> %d,%d\n", x, y, mx, my);
                    lastX = x;
                    lastY = y;
                }
            }
            gtWrite(0x814E, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
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

// No button the ESP32 can read: the PaperS3's belongs to its PMS150G, the round board has
// only reset and boot.
int buttonPressed() { return 0; }

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
    if (!busUp()) return false;
    bmi270_driver_config_t dc = {};
    dc.addr = BMI270_I2C_ADDRESS_L;
    dc.interface = BMI270_USE_I2C;
    dc.i2c_bus = sBus;
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

// ── Clock: the BM8563 RTC (0x51), kept in UTC. ──

static i2c_master_dev_handle_t sRtc;

static bool rtcUp() {
    if (sRtc) return true;
    if (!busUp() || i2c_master_probe(sBus, 0x51, 50) != ESP_OK) return false;
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = 0x51;
    dev.scl_speed_hz = 400000;
    return i2c_master_bus_add_device(sBus, &dev, &sRtc) == ESP_OK;
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
    uint8_t reg = 0x02, b[7];
    if (!rtcUp() || i2c_master_transmit_receive(sRtc, &reg, 1, b, 7, 50) != ESP_OK) return false;
    if (b[0] & 0x80) return false;   // VL: the clock stopped at some point (no backup power)
    const int year = fromBcd(b[6]) + ((b[5] & 0x80) ? 1900 : 2000);
    const int64_t days = daysFromCivil(year, fromBcd(b[5] & 0x1F), fromBcd(b[3] & 0x3F));
    *epoch = days * 86400 + fromBcd(b[2] & 0x3F) * 3600 + fromBcd(b[1] & 0x7F) * 60 + fromBcd(b[0] & 0x7F);
    return year >= 2024;
}

bool rtcWrite(int64_t epoch) {
    if (!rtcUp()) return false;
    const time_t t = (time_t) epoch;
    struct tm u;
    gmtime_r(&t, &u);
    const uint8_t start[2] = {0x00, 0x00};   // control 1: clock running
    const uint8_t b[8] = {0x02, toBcd(u.tm_sec), toBcd(u.tm_min), toBcd(u.tm_hour), toBcd(u.tm_mday),
                          (uint8_t) u.tm_wday, toBcd(u.tm_mon + 1), toBcd(u.tm_year % 100)};
    return i2c_master_transmit(sRtc, start, 2, 50) == ESP_OK && i2c_master_transmit(sRtc, b, 8, 50) == ESP_OK;
}

// ── Sound: a passive buzzer on GPIO21, a square wave from the LEDC. ──

static constexpr gpio_num_t kBuzzer = GPIO_NUM_21;

bool beep(int hz, int ms) {
    static std::mutex lock;
    static esp_timer_handle_t off;
    std::lock_guard<std::mutex> g(lock);
    if (!off) {
        ledc_timer_config_t t = {};
        t.speed_mode = LEDC_LOW_SPEED_MODE;
        t.duty_resolution = LEDC_TIMER_10_BIT;
        t.timer_num = LEDC_TIMER_0;
        t.freq_hz = 2000;
        t.clk_cfg = LEDC_AUTO_CLK;
        ledc_channel_config_t c = {};
        c.gpio_num = kBuzzer;
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel = LEDC_CHANNEL_0;
        c.timer_sel = LEDC_TIMER_0;
        esp_timer_create_args_t a = {};
        a.callback = [](void*) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        };
        a.name = "beep";
        if (ledc_timer_config(&t) != ESP_OK || ledc_channel_config(&c) != ESP_OK ||
            esp_timer_create(&a, &off) != ESP_OK)
            return false;
    }
    esp_timer_stop(off);
    if (ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, (uint32_t) hz) != ESP_OK) return false;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);   // 50 %: loudest
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    esp_timer_start_once(off, (uint64_t) ms * 1000);
    return true;
}

// ── Power: the battery on GPIO3 through 1/2, USB detect on GPIO5, the charger's (LGS4056H)
// status on GPIO4, low while charging. ──

static adc_oneshot_unit_handle_t sAdc;
static adc_cali_handle_t sCali;

void powerInit() {
    adc_oneshot_unit_init_cfg_t u = {};
    u.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&u, &sAdc) != ESP_OK) { sAdc = nullptr; return; }
    adc_oneshot_chan_cfg_t c = {};
    c.atten = ADC_ATTEN_DB_12;
    c.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(sAdc, kBatChannel, &c);
    adc_oneshot_config_channel(sAdc, kUsbChannel, &c);
    adc_cali_curve_fitting_config_t cc = {};
    cc.unit_id = ADC_UNIT_1;
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cc, &sCali) != ESP_OK) sCali = nullptr;
    gpio_config_t in = {};
    in.pin_bit_mask = 1ULL << kChargeStat;
    in.mode = GPIO_MODE_INPUT;
    in.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&in);
}

static int readMv(adc_channel_t ch) {
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0, mv = 0;
        adc_oneshot_read(sAdc, ch, &raw);
        if (sCali) adc_cali_raw_to_voltage(sCali, raw, &mv);
        else mv = raw * 3300 / 4095;
        sum += mv;
    }
    return sum / 8;
}

Power readPower() {
    Power p;
    if (!sAdc) return p;
    const float volts = readMv(kBatChannel) * 2.0f / 1000.0f;
    const bool usb = readMv(kUsbChannel) > 200;
    p.charging = gpio_get_level(kChargeStat) == 0;
    p.valid = true;
    p.batteryVolts = volts;
    if (volts < 2.8f) {
        p.batteryLevel = -1;
    } else {
        p.batteryLevel = (int) std::lround(std::fmax(0.0, std::fmin(100.0, (volts * 1000 - 3300) * 100 / 850)));
    }
    p.powerState = p.charging ? 1 : usb ? 2 : 0;
    return p;
}

// ── Sleep ──

static bool panelIdle(int timeoutMs) {
    for (int t = 0; t < timeoutMs; t += 20) {
        bool pending;
        xSemaphoreTake(sLock, portMAX_DELAY);
        pending = sHavePending;
        xSemaphoreGive(sLock);
        if (!pending && !sUpdating) return true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

// The BM8563's countdown timer: 1 s steps up to 255 s, then 1 min steps up to 255 min.
// Its interrupt is what switches the PaperS3 back on after a power-off. 0 stops it.
static bool rtcTimer(int seconds) {
    if (!rtcUp()) return false;
    uint8_t reg = 0x01, ctl2 = 0;
    if (i2c_master_transmit_receive(sRtc, &reg, 1, &ctl2, 1, 50) != ESP_OK) return false;
    ctl2 &= (uint8_t) ~0x0D;   // TF and AF flags clear, TIE off
    if (seconds <= 0) {
        const uint8_t off[2] = {0x0E, 0x03}, c[2] = {0x01, ctl2};
        return i2c_master_transmit(sRtc, off, 2, 50) == ESP_OK && i2c_master_transmit(sRtc, c, 2, 50) == ESP_OK;
    }
    uint8_t ctl = 0x82, count;   // enabled, 1 Hz
    if (seconds <= 255) count = (uint8_t) seconds;
    else { ctl = 0x83; count = (uint8_t) std::min(255, (seconds + 30) / 60); }   // 1/60 Hz
    const uint8_t t[3] = {0x0E, ctl, count}, c[2] = {0x01, (uint8_t) (ctl2 | 0x01)};   // TIE on
    return i2c_master_transmit(sRtc, t, 3, 50) == ESP_OK && i2c_master_transmit(sRtc, c, 2, 50) == ESP_OK;
}

const char* wakeReason() {
    static const char* reason = nullptr;
    if (reason) return reason;
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        reason = "timer";
    } else {
        switch (esp_reset_reason()) {
            case ESP_RST_POWERON: {
                // Switched on: by the RTC's timer (its flag is set) or by the button / USB.
                uint8_t reg = 0x01, ctl2 = 0;
                const bool byRtc = rtcUp() && i2c_master_transmit_receive(sRtc, &reg, 1, &ctl2, 1, 50) == ESP_OK &&
                                   (ctl2 & 0x04);
                reason = byRtc ? "rtc" : "power_on";
                break;
            }
            case ESP_RST_SW: case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT:
                reason = "restart";
                break;
            default: reason = "reset"; break;
        }
    }
    rtcTimer(0);   // the flag and the interrupt off, or the RTC holds the power line
    return reason;
}

bool sleepFor(int seconds, Sleep how) {
    if (seconds < 1) return false;
    panelIdle(10000);   // the picture stays on the panel without power; let it finish
    ESP_LOGI(TAG, "sleep %d s (%s)", seconds, how == Sleep::Light ? "light" : how == Sleep::Deep ? "deep" : "off");
    fflush(stdout);
    if (how == Sleep::Light) {
        esp_sleep_enable_timer_wakeup((uint64_t) seconds * 1000000);
        esp_light_sleep_start();
        return true;
    }
    if (how == Sleep::Off && seconds <= 255 * 60 && rtcTimer(seconds)) {
        // As M5Unified does: GPIO44 low pulses switch the power off; one isn't always enough.
        for (int i = 0; i < 5; i++) {
            gpio_set_level(kPowerOff, 0);
            esp_rom_delay_us(50000);
            gpio_set_level(kPowerOff, 1);
            esp_rom_delay_us(50000);
        }
        // Still running: on USB power. The RTC timer would switch on a board that is on
        // already; stop it and sleep instead.
        rtcTimer(0);
        ESP_LOGI(TAG, "still powered (USB?): deep sleep instead");
        fflush(stdout);
    }
    esp_sleep_enable_timer_wakeup((uint64_t) seconds * 1000000);
    esp_deep_sleep_start();
    return false;
}

// Redraw every pixel of the newest picture: clears ghosting.
bool displayRefresh() {
    if (!sLock) return false;
    sForce = true;
    xSemaphoreTake(sLock, portMAX_DELAY);
    if (!sHavePending) memcpy(sPending, sShown, (size_t) kCols * kLines);
    sHavePending = true;
    xSemaphoreGive(sLock);
    xSemaphoreGive(sKick);
    return true;
}

bool console(const std::string& line) {
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
    if (line == "touch") { touchProbe(); return true; }
    if (line == "i2c") {
        printf("i2c (SDA 41, SCL 42):");
        if (busUp())
            for (int a = 0x08; a < 0x78; a++)
                if (i2c_master_probe(sBus, a, 20) == ESP_OK) printf(" 0x%02X", a);
        printf("\n");
        return true;
    }
    if (line == "power") {
        Power p = readPower();
        printf("battery %.2f V, level %d %%, %s\n", p.batteryVolts, p.batteryLevel,
               p.powerState == 1 ? "charging" : p.powerState == 2 ? "on USB" : "on battery");
        return true;
    }
    if (line.rfind("epd", 0) == 0) {
        const std::string arg = line.size() > 4 ? line.substr(4) : "";
        if (arg == "clear") {
            displayRefresh();
        } else if (arg == "fast") {
            sMode = Mode::Fast;
        } else if (arg == "quality") {
            sMode = Mode::Quality;
        }
        printf("epd: %s mode, %d updates, last %d ms for %d pixels\n",
               sMode == Mode::Fast ? "fast" : "quality", sUpdates.load(), sLastMs.load(),
               sLastChanged.load());
        return true;
    }
    return false;
}

}  // namespace board
