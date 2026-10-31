// The board interface: everything the shared code (rcstage, the apps) needs from the
// hardware. Every board under boards/ provides a component named `board` with this header;
// the build picks one with -DBOARD=<dir>.
//
// This one: M5Stack Tab5 (ESP32-P4), 720x1280 MIPI-DSI panel with touch, ESP32-C6 radio
// over SDIO, INA226 battery monitor. Display glue over the espressif/m5stack_tab5 BSP
// (no LVGL).
#pragma once
#include <cstdint>
#include <string>

namespace board {

constexpr const char* kName = "m5stack_tab5";
constexpr const char* kDisplayName = "M5Stack Tab5  ·  ESP32-P4";   // system.board, for documents
constexpr int kWidth = 720;     // native portrait panel
constexpr int kHeight = 1280;
// ~294 dpi; Android would call that xhdpi. Documents lay out in kWidth/kDensity x
// kHeight/kDensity dp.
constexpr float kDensity = 2.0f;
constexpr bool kRound = false;   // system.screen_round: a round panel shows only the inscribed circle
constexpr bool kEink = false;   // system.screen_eink
constexpr int kGrays = 0;   // the panel's grey levels; 0 = full colour
constexpr const char* kSound = "speaker";   // what beep() plays on: "speaker", "buzzer", "" for none

bool displayInit();
void setBrightness(int percent);

// Push a frame of opaque premultiplied ARGB8888 to the panel. The source may be smaller
// than the panel by an integer factor (e.g. 360x640): the PPA scales it up while it converts
// to RGB565. `argb` must be 128-byte aligned (L2 cache line). Returns microseconds spent.
int64_t present(const uint32_t* argb, int srcW, int srcH);

// Start the same conversion without waiting; call presentWait() before reusing `argb` or
// starting another. Lets the CPU render the next frame while the PPA's DMA converts this one.
void presentAsync(const uint32_t* argb, int srcW, int srcH);
void presentWait();

// The panel's scan-out buffer (RGB565, kWidth x kHeight), for screenshots.
const uint16_t* frameBuffer();
// Print the panel to stdout for tools/snap.py: "SNAP BEGIN w h", one base64 RGB565 row per
// line, "SNAP END". Downsampled by `step`. Waits for any frame in flight first.
void dumpFrame(int step);

struct Touch {
    bool down = false;
    int x = 0, y = 0;    // panel pixels
};
Touch readTouch();

// Make stdin/stdout a blocking, driver-backed console on the board's serial port (the
// Tab5: USB-Serial-JTAG), for a console reader that uses fgetc(stdin). Apps whose framework
// owns the console already (the Muse SDK) don't call it.
void consoleInit();

// Power a radio co-processor before networking starts (the Tab5's C6 sits behind IO
// expander 0x44 P0). Boards with an on-chip radio do nothing.
void radioPower();

// A short press of a hardware button outside the touchscreen, once per press: +1 for the
// button that steps forward through documents, -1 for one that steps back, 0 for none (and
// always 0 on boards without buttons). Poll it often (each frame): it rate-limits its own
// reads.
int buttonPressed();

// ── Motion ──
// The accelerometer (m/s², gravity included) and gyroscope (rad/s) in the screen's axes, as
// Android's sensors report them: x to the right, y up the screen, z out of it (lying flat,
// face up: az = +9.81; held upright: ay = +9.81). valid = false on boards without a sensor
// (or where it isn't read yet).
struct Motion {
    bool valid = false;
    float ax = 0, ay = 0, az = 0;
    float gx = 0, gy = 0, gz = 0;
};
Motion readMotion();

// ── Clock ──
// The battery-backed real-time clock, in UTC seconds since 1970. rtcRead is false on boards
// without one, and when it lost power (its time is not to be trusted).
bool rtcRead(int64_t* epoch);
bool rtcWrite(int64_t epoch);

// ── Sound ──
// A tone of `hz` for `ms` milliseconds, played without waiting (a newer one replaces it).
// False on boards with nothing to play it on (kSound "").
bool beep(int hz, int ms);

// ── Display ──
// E-ink: redraw every pixel of the picture on the panel, which clears ghosting after many
// partial updates. False on other panels (nothing to do).
bool displayRefresh();

// ── Sleep ──
// Why the board is running: "timer" (woke from sleepFor's timer), "rtc" (switched on by the
// RTC after sleepFor's power-off), "power_on", "restart", "reset". Read it early: on the
// PaperS3 it also clears the RTC's timer flag.
const char* wakeReason();
// Light: the CPU pauses for `seconds` with everything kept (RAM, the document), and returns
// true; Wi-Fi drops and reconnects. Deep: everything off but the timer, then a full boot.
// Off: the board switches itself off and the RTC switches it on (least drain; on battery
// only: on USB it can't switch off and falls back to Deep). Deep and Off don't return.
// False if the board can't sleep that way.
enum class Sleep { Light, Deep, Off };
bool sleepFor(int seconds, Sleep how);

// ── Power ──
// Called once from the device-state sampler's task, then once a second.
void powerInit();
struct Power {
    bool valid = false;          // the board can measure its supply
    float batteryVolts = 0;
    float batteryMilliamps = 0;  // + into the battery
    int batteryLevel = -1;       // 0..100 %, -1 = no battery
    int powerState = -1;         // 0 battery, 1 charging, 2 external power, -1 unknown
    bool charging = false;
    bool headphones = false;
};
Power readPower();

// Board diagnostics on the serial console ("sys history", "expander" on the Tab5). Returns
// false for a line it doesn't know.
bool console(const std::string& line);

}  // namespace board
