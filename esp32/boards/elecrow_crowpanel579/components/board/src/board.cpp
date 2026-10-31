// Elecrow CrowPanel ESP32 5.79" E-paper: display (792x272, two SSD1683 controllers on one
// SPI bus), keys (dial up/down/press, MENU, EXIT), console (UART0 through a CH340).
//
// The panel is two 396-column halves, each driven by an SSD1683; they share CS, and the
// second ("slave") is addressed by the same commands with bit 7 set (0x24 -> 0xA4). Each
// controller has 400 sources, so the picture is kept as 800 columns with a gap of 8 at the
// seam (logical x >= 396 moves right by 8). The slave's RAM runs right to left. Data goes
// column by column: for each byte column, the 272 rows top to bottom (entry mode Y first).
// The pins, the RAM setup and the update codes are from Elecrow's factory firmware
// (github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792,
// factory_sourecode/5.79_tow/main/EPD_Init.cpp); the rest is written for this player.
#include "board/board.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"

static const char* TAG = "crowpanel";

namespace board {

// ── Pins (Elecrow's schematic and spi.h) ──
static constexpr gpio_num_t kSck = GPIO_NUM_12, kMosi = GPIO_NUM_11, kCs = GPIO_NUM_45, kDc = GPIO_NUM_46,
                            kRst = GPIO_NUM_47, kBusy = GPIO_NUM_48,   // high while busy
                            kPanelPower = GPIO_NUM_7,                  // high: the panel's 3.3 V on
                            kLed = GPIO_NUM_41;                        // the power LED
static constexpr gpio_num_t kKeyUp = GPIO_NUM_6, kKeyDown = GPIO_NUM_4, kKeyOk = GPIO_NUM_5,
                            kKeyMenu = GPIO_NUM_2, kKeyExit = GPIO_NUM_1;   // low while pressed

// ── The picture: 800 x 272, one bit per pixel, rows of 100 bytes, MSB leftmost, 1 = white ──
static constexpr int kCols = 800, kRows = 272, kRowBytes = kCols / 8, kHalfBytes = kRowBytes / 2;
static constexpr int kSeam = 396, kGap = 8;
static constexpr size_t kBytes = (size_t) kRowBytes * kRows;
static constexpr int kFullEvery = 60;   // partial refreshes between full ones (ghosting)

static spi_device_handle_t sSpi;
static uint8_t* sTx;   // one controller's RAM, in send order (DMA-capable)
// sPending: the newest frame from the player, under sLock. sTarget: what the update task is
// sending. sShown: what the panel holds.
static uint8_t *sPending, *sTarget, *sShown;
static bool sHavePending, sShownValid;
static SemaphoreHandle_t sLock, sKick;
static std::atomic<bool> sForce{false}, sUpdating{false};
// A key switched documents: the next new picture gets a full refresh.
static std::atomic<bool> sFullNext{false};
static std::atomic<int> sUpdates{0}, sFulls{0}, sLastMs{0}, sLastChanged{0};
static int sPartials;

static void send(const uint8_t* data, size_t n, bool isData) {
    gpio_set_level(kDc, isData);
    while (n) {
        const size_t k = std::min(n, (size_t) 4096);
        spi_transaction_t t = {};
        t.length = k * 8;
        t.tx_buffer = data;
        spi_device_polling_transmit(sSpi, &t);
        data += k;
        n -= k;
    }
}

static void cmd(uint8_t c, std::initializer_list<uint8_t> args = {}) {
    send(&c, 1, false);
    if (args.size()) {
        uint8_t b[8];
        size_t n = 0;
        for (uint8_t a : args) b[n++] = a;
        send(b, n, true);
    }
}

static bool waitIdle(int timeoutMs) {
    const int64_t end = esp_timer_get_time() + (int64_t) timeoutMs * 1000;
    while (gpio_get_level(kBusy)) {
        if (esp_timer_get_time() > end) {
            ESP_LOGW(TAG, "panel busy for %d ms", timeoutMs);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

// Out of deep sleep: a hardware reset, then a software one.
static void wake() {
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(kRst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(kRst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    waitIdle(1000);
    cmd(0x12);   // SWRESET
    waitIdle(1000);
}

// One controller's RAM: `slave` adds bit 7 to every command. Its byte columns, each 272 rows
// top to bottom, from `src` (or `fill` everywhere when src is null).
static void writeRam(bool slave, uint8_t ram, const uint8_t* src, uint8_t fill) {
    const uint8_t s = slave ? 0x80 : 0;
    if (!slave) {
        cmd(0x11, {0x05});                     // X+, Y-, Y first
        cmd(0x44, {0x00, 0x31});               // X bytes 0..49
        cmd(0x45, {0x0F, 0x01, 0x00, 0x00});   // Y 271..0
        cmd(0x4E, {0x00});
        cmd(0x4F, {0x0F, 0x01});
    } else {
        cmd(0x91, {0x04});                     // X-, Y-, Y first
        cmd(0xC4, {0x31, 0x00});               // X bytes 49..0
        cmd(0xC5, {0x0F, 0x01, 0x00, 0x00});
        cmd(0xCE, {0x31});
        cmd(0xCF, {0x0F, 0x01});
    }
    if (src) {
        size_t n = 0;
        const int c0 = slave ? kHalfBytes : 0;
        for (int c = c0; c < c0 + kHalfBytes; c++)
            for (int r = 0; r < kRows; r++) sTx[n++] = src[(size_t) r * kRowBytes + c];
    } else {
        memset(sTx, fill, (size_t) kHalfBytes * kRows);
    }
    cmd((uint8_t) (ram | s));
    send(sTx, (size_t) kHalfBytes * kRows, true);
}

// Send sTarget. Full: the whole panel is driven (it flashes), the "old" RAM cleared as the
// factory firmware does. Partial: the controllers compare against the old RAM (sShown) and
// move only the pixels that differ.
static void sendPicture(bool full) {
    wake();
    for (bool slave : {false, true}) {
        writeRam(slave, 0x24, sTarget, 0);
        if (full) writeRam(slave, 0x26, nullptr, 0x00);
        else writeRam(slave, 0x26, sShown, 0);
    }
    cmd(0x22, {(uint8_t) (full ? 0xF7 : 0xFF)});
    cmd(0x20);
    waitIdle(full ? 8000 : 4000);
    cmd(0x10, {0x01});   // deep sleep; the RAM is kept but the next update resets anyway
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

        const bool force = sForce.exchange(false) || sFullNext.exchange(false);
        int changed = 0;
        for (size_t i = 0; i < kBytes; i++) changed += __builtin_popcount(sShown[i] ^ sTarget[i]);
        if (!changed && !force && sShownValid) { sUpdating = false; continue; }

        // A new document (a key, or most of the panel changing) gets a clean full refresh;
        // anything else a partial one, with a full one every kFullEvery to clear the
        // ghosting. A third of the panel changing each second (iot_cycle) stays partial.
        const bool full = force || !sShownValid || changed > kWidth * kHeight / 2 || sPartials >= kFullEvery;
        const int64_t t0 = esp_timer_get_time();
        sendPicture(full);
        memcpy(sShown, sTarget, kBytes);
        sShownValid = true;
        sPartials = full ? 0 : sPartials + 1;
        if (full) sFulls++;
        sLastMs = (int) ((esp_timer_get_time() - t0) / 1000);
        sLastChanged = changed;
        sUpdates++;
        sUpdating = false;
    }
}

bool displayInit() {
    gpio_config_t out = {};
    out.pin_bit_mask = (1ULL << kPanelPower) | (1ULL << kLed) | (1ULL << kDc) | (1ULL << kRst);
    out.mode = GPIO_MODE_OUTPUT;
    gpio_config(&out);
    gpio_set_level(kPanelPower, 1);
    gpio_set_level(kLed, 1);
    gpio_set_level(kRst, 1);
    gpio_config_t in = {};
    in.pin_bit_mask = 1ULL << kBusy;
    in.mode = GPIO_MODE_INPUT;
    gpio_config(&in);

    spi_bus_config_t bus = {};
    bus.sclk_io_num = kSck;
    bus.mosi_io_num = kMosi;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = 10 * 1000 * 1000;
    dev.mode = 0;
    dev.spics_io_num = kCs;
    dev.queue_size = 1;
    if (spi_bus_add_device(SPI2_HOST, &dev, &sSpi) != ESP_OK) return false;

    sTx = (uint8_t*) heap_caps_malloc((size_t) kHalfBytes * kRows, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    sPending = (uint8_t*) heap_caps_malloc(kBytes, MALLOC_CAP_SPIRAM);
    sTarget = (uint8_t*) heap_caps_malloc(kBytes, MALLOC_CAP_SPIRAM);
    sShown = (uint8_t*) heap_caps_malloc(kBytes, MALLOC_CAP_SPIRAM);
    if (!sTx || !sPending || !sTarget || !sShown) return false;
    memset(sShown, 0xFF, kBytes);
    sLock = xSemaphoreCreateMutex();
    sKick = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(updateTask, "epd", 4096, nullptr, 4, nullptr, 0);
    ESP_LOGI(TAG, "panel up: %dx%d e-paper (2x SSD1683), busy=%d", kWidth, kHeight, gpio_get_level(kBusy));
    return true;
}

void setBrightness(int) {}

// 4x4 ordered dither thresholds: a picture converts to the same bits every time, so an
// unchanged frame costs no refresh.
static constexpr uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

void presentAsync(const uint32_t* argb, int srcW, int srcH) {
    if (!sLock) return;
    const int k = std::max(1, kWidth / std::max(1, srcW));
    xSemaphoreTake(sLock, portMAX_DELAY);
    memset(sPending, 0xFF, kBytes);
    for (int y = 0; y < kRows; y++) {
        const uint32_t* src = argb + (size_t) std::min(srcH - 1, y / k) * srcW;
        uint8_t* row = sPending + (size_t) y * kRowBytes;
        const uint8_t* bayer = &kBayer[(y & 3) << 2];
        for (int x = 0; x < kWidth; x++) {
            const uint32_t p = src[std::min(srcW - 1, x / k)];
            const int lum = (int) ((((p >> 16) & 0xFF) * 77 + ((p >> 8) & 0xFF) * 151 + (p & 0xFF) * 28) >> 8);
            if (lum > bayer[x & 3] * 16 + 8) continue;   // white: already set
            const int bx = x < kSeam ? x : x + kGap;
            row[bx >> 3] &= (uint8_t) ~(0x80 >> (bx & 7));
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
            // A step x step block's share of white, as a grey: the dither reads as grey.
            int white = 0;
            for (int dy = 0; dy < step; dy++)
                for (int dx = 0; dx < step; dx++) {
                    const int lx = x * step + dx, bx = lx < kSeam ? lx : lx + kGap;
                    white += (sShown[(size_t) (y * step + dy) * kRowBytes + (bx >> 3)] >> (7 - (bx & 7))) & 1;
                }
            const int g = white * 255 / (step * step);
            row[x] = (uint16_t) (((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
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

Touch readTouch() { return {}; }

void consoleInit() {
    uart_driver_install(UART_NUM_0, 1024, 0, 0, nullptr, 0);
    uart_vfs_dev_use_driver(UART_NUM_0);
    setvbuf(stdin, nullptr, _IONBF, 0);
}

void radioPower() {}

// ── Keys: active low; the board has pull-ups, the internal ones are on as well ──

static constexpr gpio_num_t kKeys[] = {kKeyUp, kKeyDown, kKeyOk, kKeyMenu, kKeyExit};

int buttonPressed() {
    static int64_t next = 0;
    static bool init = false, was[5] = {};
    const int64_t now = esp_timer_get_time();
    if (now < next) return 0;
    next = now + 30000;   // 30 ms: also the debounce
    if (!init) {
        gpio_config_t in = {};
        for (gpio_num_t k : kKeys) in.pin_bit_mask |= 1ULL << k;
        in.mode = GPIO_MODE_INPUT;
        in.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&in);
        for (int i = 0; i < 5; i++) was[i] = gpio_get_level(kKeys[i]) == 0;   // held at start: no press
        init = true;
        return 0;
    }
    int step = 0;
    for (int i = 0; i < 5; i++) {
        const bool down = gpio_get_level(kKeys[i]) == 0;
        if (down && !was[i]) {
            const gpio_num_t k = kKeys[i];
            if (k == kKeyDown || k == kKeyMenu) step = 1;
            else if (k == kKeyUp || k == kKeyExit) step = -1;
            else displayRefresh();
        }
        was[i] = down;
    }
    if (step) sFullNext = true;
    return step;
}

// ── No motion sensor, RTC, sound or battery gauge ──

Motion readMotion() { return {}; }
bool rtcRead(int64_t*) { return false; }
bool rtcWrite(int64_t) { return false; }
bool beep(int, int) { return false; }
void powerInit() {}
Power readPower() { return {}; }

// Redraw every pixel of the newest picture: clears ghosting.
bool displayRefresh() {
    if (!sLock) return false;
    sForce = true;
    xSemaphoreTake(sLock, portMAX_DELAY);
    if (!sHavePending) memcpy(sPending, sShown, kBytes);
    sHavePending = true;
    xSemaphoreGive(sLock);
    xSemaphoreGive(sKick);
    return true;
}

// ── Sleep ──

const char* wakeReason() {
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) return "timer";
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_SW: case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT:
            return "restart";
        default: return "reset";
    }
}

static bool panelIdle(int timeoutMs) {
    for (int t = 0; t < timeoutMs; t += 20) {
        xSemaphoreTake(sLock, portMAX_DELAY);
        const bool pending = sHavePending;
        xSemaphoreGive(sLock);
        if (!pending && !sUpdating) return true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

bool sleepFor(int seconds, Sleep how) {
    if (seconds < 1) return false;
    if (sLock) panelIdle(10000);   // the picture stays on the panel without power; let it finish
    ESP_LOGI(TAG, "sleep %d s (%s)", seconds, how == Sleep::Light ? "light" : "deep");
    fflush(stdout);
    esp_sleep_enable_timer_wakeup((uint64_t) seconds * 1000000);
    if (how == Sleep::Light) {
        esp_light_sleep_start();
        return true;
    }
    esp_deep_sleep_start();
    return false;
}

bool console(const std::string& line) {
    if (line == "keys") {
        // Five seconds of the keys' levels (1 = up), to check the wiring.
        for (int i = 0; i < 25; i++) {
            printf("keys up %d down %d ok %d menu %d exit %d  busy %d\n", gpio_get_level(kKeyUp),
                   gpio_get_level(kKeyDown), gpio_get_level(kKeyOk), gpio_get_level(kKeyMenu),
                   gpio_get_level(kKeyExit), gpio_get_level(kBusy));
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        return true;
    }
    if (line.rfind("epd", 0) == 0) {
        if (line == "epd clear") displayRefresh();
        printf("epd: %d updates (%d full), last %d ms for %d pixels, %d partial since a full one\n",
               sUpdates.load(), sFulls.load(), sLastMs.load(), sLastChanged.load(), sPartials);
        return true;
    }
    return false;
}

}  // namespace board
