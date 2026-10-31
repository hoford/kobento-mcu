#include "board/board.h"

#include "bsp/m5stack_tab5.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "mbedtls/base64.h"
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "tab5";

namespace board {

static esp_lcd_panel_handle_t sPanel = nullptr;
static esp_lcd_touch_handle_t sTouch = nullptr;
static uint16_t* sFb = nullptr;   // the DPI panel's own frame buffer (RGB565)
static ppa_client_handle_t sPpa = nullptr;
static SemaphoreHandle_t sPpaDone = nullptr;   // given by the PPA ISR when a transfer ends
static bool sPpaBusy = false;

static bool IRAM_ATTR onPpaDone(ppa_client_handle_t, ppa_event_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(sPpaDone, &woken);
    return woken == pdTRUE;
}

bool displayInit() {
    bsp_display_config_t cfg = {};
    cfg.dsi_bus.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    cfg.dsi_bus.lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;
    esp_lcd_panel_io_handle_t io = nullptr;
    if (bsp_display_new(&cfg, &sPanel, &io) != ESP_OK) {
        ESP_LOGE(TAG, "display init failed");
        return false;
    }
    void* fb = nullptr;
    if (esp_lcd_dpi_panel_get_frame_buffer(sPanel, 1, &fb) == ESP_OK) sFb = (uint16_t*) fb;
    ESP_LOGI(TAG, "panel up, frame buffer %p", fb);
    ppa_client_config_t pcfg = {};
    pcfg.oper_type = PPA_OPERATION_SRM;
    if (ppa_register_client(&pcfg, &sPpa) != ESP_OK) {
        ESP_LOGW(TAG, "PPA unavailable; using software conversion");
        sPpa = nullptr;
    } else {
        sPpaDone = xSemaphoreCreateBinary();
        ppa_event_callbacks_t cbs = {};
        cbs.on_trans_done = onPpaDone;
        ppa_client_register_event_callbacks(sPpa, &cbs);
    }

    bsp_touch_config_t tcfg = {};
    if (bsp_touch_new(&tcfg, &sTouch) != ESP_OK) {
        ESP_LOGW(TAG, "touch init failed; continuing without touch");
        sTouch = nullptr;
    }
    bsp_display_backlight_on();
    return true;
}

void setBrightness(int percent) { bsp_display_brightness_set(percent); }

const uint16_t* frameBuffer() { return sFb; }

static void softwarePresent(const uint32_t* argb, int srcW, int srcH) {
    const int step = kWidth / srcW;
    for (int y = 0; y < kHeight; y++) {
        const uint32_t* src = argb + (size_t) (y / step) * srcW;
        uint16_t* dst = sFb + (size_t) y * kWidth;
        for (int x = 0; x < kWidth; x++) {
            uint32_t p = src[x / step];
            dst[x] = (uint16_t) (((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
        }
    }
    esp_cache_msync(sFb, (size_t) kWidth * kHeight * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

static bool ppaStart(const uint32_t* argb, int srcW, int srcH, ppa_trans_mode_t mode) {
    // PPA scale-rotate-mirror: ARGB8888 (ThorVG's little-endian BGRA bytes) -> RGB565,
    // integer upscale, written straight into the scan-out buffer by DMA.
    ppa_srm_oper_config_t op = {};
    op.in.buffer = argb;
    op.in.pic_w = srcW;
    op.in.pic_h = srcH;
    op.in.block_w = srcW;
    op.in.block_h = srcH;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
    op.out.buffer = sFb;
    op.out.buffer_size = (uint32_t) kWidth * kHeight * 2;
    op.out.pic_w = kWidth;
    op.out.pic_h = kHeight;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    op.scale_x = (float) kWidth / srcW;
    op.scale_y = (float) kHeight / srcH;
    op.mode = mode;
    // Make the CPU-written pixels visible to the PPA's DMA.
    esp_cache_msync((void*) argb, (size_t) srcW * srcH * 4,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    esp_err_t err = ppa_do_scale_rotate_mirror(sPpa, &op);
    if (err == ESP_OK) return true;
    ESP_LOGW(TAG, "PPA failed (%s); falling back to software", esp_err_to_name(err));
    ppa_unregister_client(sPpa);
    sPpa = nullptr;
    return false;
}

void presentWait() {
    if (sPpaBusy) {
        xSemaphoreTake(sPpaDone, portMAX_DELAY);
        sPpaBusy = false;
    }
}

void presentAsync(const uint32_t* argb, int srcW, int srcH) {
    presentWait();
    if (!sFb) return;
    if (sPpa && ppaStart(argb, srcW, srcH, PPA_TRANS_MODE_NON_BLOCKING)) {
        sPpaBusy = true;
        return;
    }
    softwarePresent(argb, srcW, srcH);
}

int64_t present(const uint32_t* argb, int srcW, int srcH) {
    int64_t t0 = esp_timer_get_time();
    presentAsync(argb, srcW, srcH);
    presentWait();
    return esp_timer_get_time() - t0;
}

Touch readTouch() {
    Touch t;
    if (!sTouch) return t;
    esp_lcd_touch_read_data(sTouch);
    esp_lcd_touch_point_data_t pts[1];
    uint8_t cnt = 0;
    if (esp_lcd_touch_get_data(sTouch, pts, &cnt, 1) == ESP_OK && cnt > 0) {
        t.down = true;
        t.x = pts[0].x;
        t.y = pts[0].y;
    }
    return t;
}

void dumpFrame(int step) {
    presentWait();
    const uint16_t* fb = frameBuffer();
    const int w = kWidth, h = kHeight;
    const int ow = w / step, oh = h / step;
    // One row of RGB565 little-endian, base64'd per line so nothing in the console path
    // (CR/LF translation, log interleaving) can corrupt binary data.
    static uint16_t row[1024];
    static unsigned char b64[2800];
    printf("\nSNAP BEGIN %d %d\n", ow, oh);
    for (int y = 0; y < oh; y++) {
        const uint16_t* src = fb + (size_t) y * step * w;
        for (int x = 0; x < ow; x++) row[x] = src[x * step];
        size_t olen = 0;
        mbedtls_base64_encode(b64, sizeof(b64), &olen, (const unsigned char*) row, ow * 2);
        b64[olen] = 0;
        printf("%s\n", b64);
        if ((y & 15) == 15) fflush(stdout);
    }
    printf("SNAP END\n");
    fflush(stdout);
}

}  // namespace board

namespace board {

void consoleInit() {
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 1024;
    cfg.tx_buffer_size = 4096;
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, nullptr, _IONBF, 0);
}

// The Tab5's buttons are reset and the PMU's power key, which isn't wired to the P4.
int buttonPressed() { return 0; }

void radioPower() {
    // The ESP32-C6 is powered through IO expander 0x44 P0; it must be up before esp_hosted.
    if (bsp_feature_enable(BSP_FEATURE_WIFI, true) != ESP_OK) ESP_LOGE(TAG, "C6 power on failed");
}

}  // namespace board
