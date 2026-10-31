// The device's state, published to RemoteCompose documents (docs/RC_SYSTEM_VARIABLES.md).
//
// System variables (RemoteContext::ID_*, proposed ids 38..41):
//   batteryLevel   0..100 %, from the board; -1 with no battery
//   powerState     0 on battery, 1 charging, 2 external power, -1 unknown
//   memoryFree     free heap (internal + PSRAM) in MB
//   networkSignal  Wi-Fi strength 0..100 %, 0 when not connected
//
// Named variables, sent to documents that declare them: system.battery_voltage,
// system.battery_current, system.charging, system.headphones, system.wifi_rssi,
// system.wifi_ssid, system.ip, system.memory_internal_kb, system.memory_psram_kb,
// system.temperature, system.uptime, system.board (the board's display name).
#pragma once

#include <string>

#include "board/board.h"

namespace rcstage {

// Start the sampler (after rcstage::start; Wi-Fi may come up later). It reads every second;
// battery, power, memory, signal strength and temperature reach documents every 10 s (each
// change costs a full repaint), going online/offline at once.
void deviceStateStart();

// The last values, for logs and the console.
struct DeviceState {
    board::Power power;
    int wifiRssi = 0;
    float memoryFreeMb = 0;
    float temperatureC = 0;
};
DeviceState deviceState();

// Where the system time came from: "ntp" (SNTP set it; synced), "rtc" (the board's clock
// chip, at boot, before the network), or "none" (the time is not to be trusted). lastSync:
// the system time of the last SNTP sync, 0 = none. rtc / rtcEpoch: the clock chip, read now
// (false without one, or when it lost power).
struct ClockState {
    std::string source = "none";
    bool synced = false;
    int64_t lastSync = 0;
    bool rtc = false;
    int64_t rtcEpoch = 0;
};
ClockState clockState();

// Set the system time from the board's RTC, if it has a valid one and nothing set the time
// yet. deviceStateStart does it too; call it first to draw a clock right at boot.
void clockFromRtc();

}  // namespace rcstage
