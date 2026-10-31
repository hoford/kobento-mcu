// The board interface: everything the shared code (rcstage, the apps) needs from the
// hardware. Every board under boards/ provides a component named `board` with this header;
// the build picks one with -DBOARD=<dir>.
//
// This one: M5Stack PaperS3 (ESP32-S3R8: 8 MB octal PSRAM in the package, 16 MB flash), a
// 4.7" 960x540 e-ink panel (ED047TC1) on an 8-bit parallel bus, used in portrait, with a
// GT911 touch panel. No backlight. Wi-Fi and BLE are on the chip. See ../../README.md.
#pragma once
#include <cstdint>
#include <string>

namespace board {

constexpr const char* kName = "m5stack_papers3";
constexpr const char* kDisplayName = "M5Stack PaperS3  ·  ESP32-S3 e-ink";   // system.board, for documents
constexpr int kWidth = 540;    // portrait: the panel's 960-pixel side is the height
constexpr int kHeight = 960;
// ~234 dpi; documents lay out in 360 x 640 dp, like the Tab5.
constexpr float kDensity = 1.5f;
constexpr bool kRound = false;
constexpr bool kEink = true;    // system.screen_eink: slow refresh, 16 greys; documents use a paper palette
constexpr int kGrays = 16;   // the panel's grey levels; 0 = full colour
constexpr const char* kSound = "buzzer";   // what beep() plays on: "speaker", "buzzer", "" for none

bool displayInit();
void setBrightness(int percent);   // no backlight: does nothing

// Hand a frame of opaque premultiplied ARGB8888 to the panel. It is converted to 16 grey
// levels at once, and an update task on core 0 refreshes the pixels that changed (about
// 0.4 s); frames that arrive meanwhile replace each other, and the newest is shown next.
// The source may be smaller than the panel by an integer factor. Returns microseconds spent.
int64_t present(const uint32_t* argb, int srcW, int srcH);

// The same: the frame is copied before it returns, so presentWait() returns at once.
void presentAsync(const uint32_t* argb, int srcW, int srcH);
void presentWait();

// No RGB scan-out buffer: the panel holds its own picture.
const uint16_t* frameBuffer();
// Print what the panel shows (its grey levels) to stdout for tools/snap.py: "SNAP BEGIN w h",
// one base64 RGB565 row per line, "SNAP END". Downsampled by `step`.
void dumpFrame(int step);

struct Touch {
    bool down = false;
    int x = 0, y = 0;    // panel pixels
};
Touch readTouch();

// Make stdin/stdout a blocking, driver-backed console on the board's serial port (here:
// the chip's USB Serial/JTAG), for a console reader that uses
// fgetc(stdin). Apps whose framework owns the console already (the Muse SDK) don't call it.
void consoleInit();

// Power a radio co-processor before networking starts. This radio is on-chip.
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

// Board diagnostics on the serial console ("touch", "i2c", "power", "epd"). Returns false for a line it
// doesn't know.
bool console(const std::string& line);

}  // namespace board
