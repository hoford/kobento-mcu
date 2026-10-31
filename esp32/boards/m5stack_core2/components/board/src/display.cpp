// Core2 display (ILI9342C on SPI), touch (FT6336U on I2C) and console (UART0).
//
// Pins, the panel init and the touch registers follow M5GFX (M5GFX.cpp, board_M5StackCore2;
// lgfx/v1/panel/Panel_ILI9342.hpp; lgfx/v1/touch/Touch_FT5x06.cpp).
#include "board/board.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "core2.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"

static const char* TAG = "core2";

namespace board {

static constexpr gpio_num_t kMosi = GPIO_NUM_23, kMiso = GPIO_NUM_38, kSclk = GPIO_NUM_18,
                            kLcdCs = GPIO_NUM_5, kLcdDc = GPIO_NUM_15, kSdCs = GPIO_NUM_4;
static constexpr int kLcdHz = 40 * 1000 * 1000;
#if CONFIG_CORE2_ROTATE_180
static constexpr bool kRotate180 = true;
#else
static constexpr bool kRotate180 = false;
#endif
// Frames go out in strips converted to RGB565 in internal RAM: the classic ESP32's SPI DMA
// can't read PSRAM. Two buffers, so one converts while the other is on the wire.
static constexpr int kStripLines = 16;
static constexpr size_t kStripBytes = (size_t) kWidth * kStripLines * 2;

static esp_lcd_panel_io_handle_t sIo;
static uint16_t* sStrip[2];
static SemaphoreHandle_t sFree;   // counts strip buffers not on the wire
static const uint32_t* sLast;     // last presented frame, for dumpFrame
static int sLastW, sLastH;
static i2c_master_dev_handle_t sTouch;

static bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(sFree, &woken);
    return woken == pdTRUE;
}

static void cmd(uint8_t c, std::initializer_list<uint8_t> data = {}) {
    esp_lcd_panel_io_tx_param(sIo, c, data.size() ? data.begin() : nullptr, data.size());
}

static void touchInit() {
    sTouch = i2cDevice(0x38);
    uint8_t id[6] = {};
    if (!i2cWrite(sTouch, 0x00, 0x00) || !i2cRead(sTouch, 0xA3, id, sizeof id) || !id[5]) {
        ESP_LOGW(TAG, "no FT6336 touch at 0x38");
        sTouch = nullptr;
        return;
    }
    i2cWrite(sTouch, 0xA4, 0x00);   // polling mode
    gpio_config_t in = {};
    in.pin_bit_mask = 1ULL << GPIO_NUM_39;
    in.mode = GPIO_MODE_INPUT;
    gpio_config(&in);
}

bool displayInit() {
    if (pmuInit() == Pmu::None) return false;

    // Keep the SD card (CS 4) off the shared bus and the panel deselected until it's set up.
    gpio_config_t out = {};
    out.pin_bit_mask = (1ULL << kSdCs) | (1ULL << kLcdCs);
    out.mode = GPIO_MODE_OUTPUT;
    gpio_config(&out);
    gpio_set_level(kSdCs, 1);
    gpio_set_level(kLcdCs, 1);

    spi_bus_config_t bus = {};
    bus.mosi_io_num = kMosi;
    bus.miso_io_num = kMiso;
    bus.sclk_io_num = kSclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kStripBytes;
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

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
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI3_HOST, &io, &sIo) != ESP_OK) return false;

    for (auto& s : sStrip) {
        s = (uint16_t*) heap_caps_malloc(kStripBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s) return false;
    }

    // ILI9342C init, as M5GFX's Panel_ILI9342 (+ invert on, RGB565, MADCTL below).
    cmd(0xC8, {0xFF, 0x93, 0x42});
    cmd(0xC0, {0x12, 0x12});
    cmd(0xC1, {0x03});
    cmd(0xC5, {0xF2});
    cmd(0xB0, {0xE0});
    cmd(0xF6, {0x01, 0x00, 0x00});
    cmd(0xE0, {0x00, 0x0C, 0x11, 0x04, 0x11, 0x08, 0x37, 0x89, 0x4C, 0x06, 0x0C, 0x0A, 0x2E, 0x34, 0x0F});
    cmd(0xE1, {0x00, 0x0B, 0x11, 0x05, 0x13, 0x09, 0x33, 0x67, 0x48, 0x07, 0x0E, 0x0B, 0x2E, 0x33, 0x0F});
    cmd(0xB6, {0x08, 0x82, 0x1D, 0x04});
    cmd(0x38);   // IDMOFF
    cmd(0x11);   // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x21);   // INVON
    cmd(0x3A, {0x55});
    // MADCTL: BGR (0x08), M5GFX's standard orientation with the three-button touch strip
    // below the screen; MY|MX turn it 180 degrees (verified by eye, 2026-10-18).
    cmd(0x36, {(uint8_t) (kRotate180 ? 0xC8 : 0x08)});
    cmd(0x29);   // DISPON

    // Clear to black before the backlight comes on.
    memset(sStrip[0], 0, kStripBytes);
    cmd(0x2A, {0, 0, (kWidth - 1) >> 8, (kWidth - 1) & 0xFF});
    for (int y = 0; y < kHeight; y += kStripLines) {
        xSemaphoreTake(sFree, portMAX_DELAY);
        const int y1 = std::min(kHeight, y + kStripLines) - 1;
        cmd(0x2B, {(uint8_t) (y >> 8), (uint8_t) (y & 0xFF), (uint8_t) (y1 >> 8), (uint8_t) (y1 & 0xFF)});
        esp_lcd_panel_io_tx_color(sIo, 0x2C, sStrip[0], kStripBytes);
    }
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreGive(sFree);
    xSemaphoreGive(sFree);

    touchInit();
    pmuBacklight(100);
    ESP_LOGI(TAG, "panel up: %dx%d ILI9342C at %d MHz", kWidth, kHeight, kLcdHz / 1000000);
    return true;
}

void setBrightness(int percent) { pmuBacklight(percent); }

// Premultiplied, opaque ARGB8888 -> RGB565, byte-swapped for the panel.
static inline uint16_t toPanel565(uint32_t p) {
    const uint16_t v = (uint16_t) (((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
    return (uint16_t) ((v << 8) | (v >> 8));
}

// Convert and send one frame (the sender task, or present()).
static void sendFrame(const uint32_t* argb, int srcW, int srcH) {
    const int k = std::max(1, kWidth / std::max(1, srcW));   // integer upscale factor
    cmd(0x2A, {0, 0, (kWidth - 1) >> 8, (kWidth - 1) & 0xFF});
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
        // Each strip gets its own row window and a plain RAMWR: this panel ignores
        // Memory Write Continue (0x3C), which left all but the first strip unwritten.
        const int y1 = y0 + lines - 1;
        cmd(0x2B, {(uint8_t) (y0 >> 8), (uint8_t) (y0 & 0xFF), (uint8_t) (y1 >> 8), (uint8_t) (y1 & 0xFF)});
        esp_lcd_panel_io_tx_color(sIo, 0x2C, dst, (size_t) lines * kWidth * 2);
        which ^= 1;
    }
    // Both strips back: the frame is fully on the panel.
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreTake(sFree, portMAX_DELAY);
    xSemaphoreGive(sFree);
    xSemaphoreGive(sFree);
}

// A sender task on core 0 converts and sends frame N while the player (core 1) renders N+1:
// the SPI wire alone is ~31 ms a frame at 40 MHz.
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
    xSemaphoreTake(sIdle, portMAX_DELAY);   // the previous frame is out
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

static std::atomic<int> sMarkStep{0};
static bool sMarkDown = false;

int touchMarkStep() { return sMarkStep.exchange(0); }

Touch readTouch() {
    Touch t;
    if (!sTouch) return t;
    uint8_t d[5] = {};
    if (!i2cRead(sTouch, 0x02, d, sizeof d) || (d[0] & 0x0F) == 0) {
        sMarkDown = false;
        return t;
    }
    const int x = ((d[1] & 0x0F) << 8) | d[2];
    const int y = ((d[3] & 0x0F) << 8) | d[4];
    if (x >= kWidth || y >= kHeight) {   // rows 240..279: the three button marks
        // In raw coordinates A is on the left; with the panel turned, it's on the right.
        const bool a = kRotate180 ? x >= kWidth * 2 / 3 : x < kWidth / 3;
        if (y >= kHeight && x < kWidth && !sMarkDown) sMarkStep = a ? -1 : 1;
        sMarkDown = true;
        return t;
    }
    sMarkDown = false;
    // Raw coordinates match M5's standard orientation; a turned panel turns them too.
    t.down = true;
    t.x = kRotate180 ? kWidth - 1 - x : x;
    t.y = kRotate180 ? kHeight - 1 - y : y;
    return t;
}

// `touch` on the console: print the controller's raw coordinates for 15 s, to map the glass
// (the printed button marks, or the blank bezel, report rows 240..279).
void touchProbe() {
    if (!sTouch) { printf("touch: no controller\n"); return; }
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

}  // namespace board
