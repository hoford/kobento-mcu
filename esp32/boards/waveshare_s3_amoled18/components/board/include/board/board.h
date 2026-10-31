// The board interface: everything the shared code (rcstage, the apps) needs from the
// hardware. Every board under boards/ provides a component named `board` with this header;
// the build picks one with -DBOARD=<dir>.
//
// This one: Waveshare ESP32-S3-Touch-AMOLED-1.8 (ESP32-S3R8: 8 MB octal PSRAM in the package,
// 16 MB flash), a 1.8" 368x448 AMOLED on QSPI: SH8601 with FT3168 touch on the first boards,
// CO5300 with CST816 touch on v2 (told apart at boot by the touch chip). AXP2101 PMU. Wi-Fi
// and BLE are on the chip. See ../../README.md.
#pragma once
#include <cstdint>
#include <string>

namespace board {

constexpr const char* kName = "waveshare_s3_amoled18";
constexpr const char* kDisplayName = "Waveshare 1.8\" AMOLED  ·  ESP32-S3";   // system.board, for documents
constexpr int kWidth = 368;      // native portrait panel
constexpr int kHeight = 448;
// ~320 dpi, a watch-sized screen held close: documents lay out in 245 x 299 dp, about the
// round board's 240 dp across.
constexpr float kDensity = 1.5f;
constexpr bool kRound = false;  // system.screen_round: a round panel shows only the inscribed circle
constexpr bool kEink = false;   // system.screen_eink
constexpr int kGrays = 0;   // the panel's grey levels; 0 = full colour
constexpr const char* kSound = "";   // what beep() plays on: "speaker", "buzzer", "" for none

bool displayInit();
void setBrightness(int percent);

// Push a frame of opaque premultiplied ARGB8888 to the panel. The source may be smaller
// than the panel by an integer factor; it is scaled up while it is converted to RGB565 in
// strips in internal RAM. Returns microseconds spent.
int64_t present(const uint32_t* argb, int srcW, int srcH);

// Start the same on a sender task (core 0) and return, so the player renders the next frame
// meanwhile; call presentWait() before reusing `argb`. presentAsync() waits for the previous
// frame first.
void presentAsync(const uint32_t* argb, int srcW, int srcH);
void presentWait();

// No scan-out buffer to read back: the panel's memory is behind SPI.
const uint16_t* frameBuffer();
// Print the last presented frame to stdout for tools/snap.py: "SNAP BEGIN w h", one base64
// RGB565 row per line, "SNAP END". Downsampled by `step`.
void dumpFrame(int step);

struct Touch {
    bool down = false;
    int x = 0, y = 0;    // panel pixels
};
Touch readTouch();

// Make stdin/stdout a blocking, driver-backed console on the chip's USB Serial/JTAG, for a
// console reader that uses
// fgetc(stdin). Apps whose framework owns the console already (the Muse SDK) don't call it.
void consoleInit();

// Power a radio co-processor before networking starts. This radio is on-chip.
void radioPower();

// A short press, once per press: +1 for the PWR key (forward; latched by the AXP2101), -1 for
// BOOT (back; GPIO0), 0 for none. Apps use them to step through documents. Poll it often
// (each frame): it rate-limits its own reads.
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

// Board diagnostics on the serial console ("touch", "i2c", "power"). Returns false for a line it
// doesn't know.
bool console(const std::string& line);

}  // namespace board
