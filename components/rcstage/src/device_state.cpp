#include "rcstage/device_state.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <ctime>
#include <sys/time.h>
#include <string>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#endif

#include "rccore/RemoteContext.h"
#include "rcstage/stage.h"

namespace rcstage {

using rccore::RemoteContext;

// Sampled every second; battery, power, memory, signal and temperature published every 10.
static constexpr int kSlowEvery = 10;

static std::mutex sLock;
static DeviceState sLast;
#if SOC_TEMP_SENSOR_SUPPORTED
static temperature_sensor_handle_t sTemp;
#endif

// The clock, for documents that show the time: from the board's RTC at boot when it has
// one, then set by SNTP once Wi-Fi has an address and kept by it (hourly), each sync written
// back to the RTC. Neither player's network code sets it otherwise.
static std::string sClockSource = "none";   // sLock
static int64_t sLastSync = 0;

static void logClock(const char* how) {
    time_t t = time(nullptr);
    struct tm local;
    localtime_r(&t, &local);
    char b[32];
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S %Z", &local);
    ESP_LOGI("rcstage", "clock set from %s: %s", how, b);
}

void clockFromRtc() {
    int64_t e = 0;
    if (time(nullptr) > 1700000000 || !board::rtcRead(&e)) return;
    struct timeval tv = {(time_t) e, 0};
    settimeofday(&tv, nullptr);
    {
        std::lock_guard<std::mutex> g(sLock);
        sClockSource = "rtc";
    }
    logClock("the RTC");
}

static void startClock() {
    if (!*CONFIG_RCSTAGE_NTP_SERVER) return;
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_RCSTAGE_NTP_SERVER);
    c.sync_cb = [](struct timeval*) {
        const time_t t = time(nullptr);
        {
            std::lock_guard<std::mutex> g(sLock);
            sClockSource = "ntp";
            sLastSync = t;
        }
        logClock("NTP");
        if (!board::rtcWrite(t)) ESP_LOGD("rcstage", "no RTC to keep the time");
    };
    if (esp_netif_sntp_init(&c) != ESP_OK) ESP_LOGW("rcstage", "SNTP didn't start");
}

static std::string fmt(float v, int decimals) {
    char b[24];
    snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}

static void sample() {
    DeviceState s;
    s.power = board::readPower();
    const board::Power& p = s.power;

    size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s.memoryFreeMb = (internal + psram) / (1024.0f * 1024.0f);
#if SOC_TEMP_SENSOR_SUPPORTED
    if (sTemp) temperature_sensor_get_celsius(sTemp, &s.temperatureC);
#endif

    wifi_ap_record_t ap = {};
    bool up = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    s.wifiRssi = up ? ap.rssi : 0;
    int signal = up ? std::max(1, std::min(100, 2 * (ap.rssi + 100))) : 0;
    std::string ip;
    if (esp_netif_t* n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(n, &info) == ESP_OK && info.ip.addr) {
            char b[16];
            snprintf(b, sizeof b, IPSTR, IP2STR(&info.ip));
            ip = b;
        }
    }

    static bool sClock = false;
    if (!ip.empty() && !sClock) {
        sClock = true;
        startClock();
    }

    // Every change repaints the whole document (~400 ms at 720x1280 on the Tab5), so values
    // that drift are published only every kSlowEvery samples. Connectivity and the Wi-Fi name
    // go out at once: they change rarely and matter when they do.
    static int sTick = 0;
    static bool sWasUp = false;
    const bool slow = sTick++ % kSlowEvery == 0;
    if (slow || up != sWasUp) setSystem(RemoteContext::ID_NETWORK_SIGNAL, (float) signal);
    sWasUp = up;
    setNamed("system.wifi_ssid", up ? std::string((const char*) ap.ssid) : std::string());
    setNamed("system.ip", ip);
    setNamed("system.headphones", p.headphones ? "1" : "0");
    setNamed("system.uptime", std::to_string(esp_timer_get_time() / 1000000));
    setNamed("system.board", board::kDisplayName);
    setNamed("system.screen_round", board::kRound ? "1" : "0");
    setNamed("system.screen_eink", board::kEink ? "1" : "0");
    if (slow) {
        setSystem(RemoteContext::ID_BATTERY_LEVEL, (float) p.batteryLevel);
        setSystem(RemoteContext::ID_POWER_STATE, (float) p.powerState);
        setSystem(RemoteContext::ID_MEMORY_FREE, std::round(s.memoryFreeMb * 10) / 10);
        if (p.valid) {
            setNamed("system.battery_voltage", fmt(p.batteryVolts, 2));
            setNamed("system.battery_current", fmt(p.batteryMilliamps, 0));
            setNamed("system.charging", p.charging ? "1" : "0");
        }
        setNamed("system.wifi_rssi", std::to_string(s.wifiRssi));
        setNamed("system.memory_internal_kb", std::to_string(internal / 1024));
        setNamed("system.memory_psram_kb", std::to_string(psram / 1024));
        setNamed("system.temperature", fmt(s.temperatureC, 1));
    }

    std::lock_guard<std::mutex> g(sLock);
    sLast = s;
}

static void task(void*) {
    board::powerInit();
    clockFromRtc();
    while (true) {
        sample();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void deviceStateStart() {
    setenv("TZ", CONFIG_RCSTAGE_TIMEZONE, 1);
    tzset();
#if SOC_TEMP_SENSOR_SUPPORTED
    temperature_sensor_config_t tc = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tc, &sTemp) != ESP_OK || temperature_sensor_enable(sTemp) != ESP_OK)
        sTemp = nullptr;
#endif
    xTaskCreate(task, "devstate", 4096, nullptr, 3, nullptr);
}

ClockState clockState() {
    ClockState c;
    {
        std::lock_guard<std::mutex> g(sLock);
        c.source = sClockSource;
        c.lastSync = sLastSync;
    }
    c.synced = c.source == "ntp";
    c.rtc = board::rtcRead(&c.rtcEpoch);
    return c;
}

DeviceState deviceState() {
    std::lock_guard<std::mutex> g(sLock);
    return sLast;
}

}  // namespace rcstage
