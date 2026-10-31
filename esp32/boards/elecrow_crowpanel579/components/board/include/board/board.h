// The board interface: everything the shared code (rcstage, the apps) needs from the
// hardware. Every board under boards/ provides a component named `board` with this header;
// the build picks one with -DBOARD=<dir>.
//
// This one: Elecrow CrowPanel ESP32 5.79" E-paper HMI Display (ESP32-S3-WROOM-1-N8R8: 8 MB
// flash, 8 MB octal PSRAM), a 792x272 black-and-white panel driven by two SSD1683
// controllers over SPI, landscape with the keys on the right: a dial (up, down, press),
// MENU and EXIT. No touch, no battery gauge, no backlight. Console on UART0 through a CH340.
// See ../../README.md.
#pragma once
#include <cstdint>
#include <string>

namespace board {

constexpr const char* kName = "elecrow_crowpanel579";
constexpr const char* kDisplayName = "CrowPanel 5.79\u2033  \u00b7  ESP32-S3 e-paper";   // system.board, for documents
constexpr int kWidth = 792;
constexpr int kHeight = 272;
// ~145 dpi: documents lay out in 792 x 272 dp.
constexpr float kDensity = 1.0f;
constexpr bool kRound = false;
constexpr bool kEink = true;    // system.screen_eink: slow refresh; documents use a paper palette
constexpr int kGrays = 2;   // the panel's grey levels: black and white (greys are dithered)
constexpr const char* kSound = "";   // what beep() plays on: "speaker", "buzzer", "" for none

bool displayInit();
void setBrightness(int percent);   // no backlight: does nothing

// Hand a frame of opaque premultiplied ARGB8888 to the panel. It is dithered to black and
// white at once, and an update task on core 0 sends it to the controllers: a partial
// refresh (about 0.5 s) when little changed, a full one (about 3 s, it flashes) when most
// did, and every tenth time. Frames that arrive meanwhile replace each other. The source may
// be smaller than the panel by an integer factor. Returns microseconds spent.
int64_t present(const uint32_t* argb, int srcW, int srcH);

// The same: the frame is copied before it returns, so presentWait() returns at once.
void presentAsync(const uint32_t* argb, int srcW, int srcH);
void presentWait();

// No RGB scan-out buffer: the panel holds its own picture.
const uint16_t* frameBuffer();
// Print what the panel shows to stdout for tools/snap.py: "SNAP BEGIN w h", one base64
// RGB565 row per line, "SNAP END". Downsampled by `step`.
void dumpFrame(int step);

struct Touch {
    bool down = false;
    int x = 0, y = 0;    // panel pixels
};
Touch readTouch();   // no touch panel: never down

// Make stdin/stdout a blocking, driver-backed console on the board's serial port (here:
// UART0, through the CH340), for a console reader that uses fgetc(stdin).
void consoleInit();

// Power a radio co-processor before networking starts. This radio is on-chip.
void radioPower();

// A short press of a hardware button outside the touchscreen, once per press: +1 for the
// button that steps forward through documents, -1 for one that steps back, 0 for none.
// Here: dial down and MENU +1, dial up and EXIT -1; pressing the dial redraws the panel in
// full (clears ghosting) and returns 0. Poll it often (each frame): it rate-limits its own
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
// Why the board is running: "timer" (woke from sleepFor's timer), "power_on", "restart",
// "reset".
const char* wakeReason();
// Light: the CPU pauses for `seconds` with everything kept (RAM, the document), and returns
// true; Wi-Fi drops and reconnects. Deep: everything off but the timer, then a full boot.
// Off: here the same as Deep (no RTC to switch the board on). Deep and Off don't return.
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

// Board diagnostics on the serial console ("epd", "keys"). Returns false for a line it
// doesn't know.
bool console(const std::string& line);

}  // namespace board
