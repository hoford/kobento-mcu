// rcstage: one RemoteCompose document live on the board's screen.
//
//   rccore (upstream engine) -> ThorvgPaintContext (ARGB8888) -> board::present -> panel
//
// A task on core 1 owns the engine. It forwards touch to the document, repaints only when
// the document asks to (getRepaintDelay / needsRepaint) or something changed, and draws an
// optional status banner on top. Other tasks hand it work through the thread-safe calls.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rcstage {

struct Hooks {
    void (*tick)() = nullptr;                 // player task, every loop: the host's own work
    void (*longPress)() = nullptr;            // a still press held 1.5 s
    void (*tap)(float xDp, float yDp) = nullptr;   // a short still press, after the doc's onClick
    // A document's hostAction(id, metadata) or HostNamedAction(name, value), run from a
    // click. Player task; hand slow work off. Named-action types: -1 none, 0 float,
    // 1 int, 2 string.
    void (*hostAction)(int id, const std::string& metadata) = nullptr;
    void (*namedAction)(const std::string& name, int type, const std::string& value) = nullptr;
};

// Call after board::displayInit(). Shows `doc` first (copied).
// false if `doc` didn't load: call again with another.
bool start(const Hooks& hooks, const uint8_t* doc, size_t size, const char* name, bool fit = false);

// ── Any task ──
// Replace the document from the next frame. Returns at once; a document that fails to
// load is logged and the current one stays.
// fit: lay the document out at its own size (its header's width and height, else 600 x 600)
// and scale it to fit the screen, centred on black, rather than at the screen's size. For
// documents designed at one size (a 360 x 360 example on the Core2's 320 x 240), and for
// size-less ones drawn for a phone canvas in pixels.
void show(std::vector<uint8_t> bytes, std::string name, bool fit = false);
// A banner across the bottom of the screen; an empty title hides it. `argb` tints its
// accent bar (0 = neutral).
// A document that declares the named strings system.banner_title / system.banner_detail
// (and optionally the color system.banner_color) receives them instead, and no overlay
// is drawn over it.
void setBanner(const std::string& title, const std::string& detail = {}, uint32_t argb = 0);
// Set a document variable by its exported name (NAMED_VARIABLE): a number or, for text
// variables, a string. Sticky, like RemoteContext.setNamed*Override: it is applied to every
// document loaded later that declares the name, and ignored by those that don't.
void setNamed(std::string name, std::string value);
// Listen to a class of named variables: fn(name, value) runs when a named variable of the
// showing document whose name matches `pattern` was changed by a touch: a click's value
// change, or a value the document exports that follows a drag. It runs once the gesture is
// over (0.3 s after the finger lifts), once per variable that ended up different. Changes
// made any other way (setNamed, a clock, paging with buttons) are not reported. A pattern
// ending in "*" matches by prefix ("Control.*"); else the whole name. The value is text, as
// namedValues gives it. Player task. Register before start(); listeners stay.
// The player sets system.touch_down (1 while a finger is down) for documents that declare it.
using NamedListener = void (*)(const std::string& name, const std::string& value);
void listenNamed(std::string pattern, NamedListener fn);
// Whether the showing document declares a named variable matching `pattern` (as above): it
// takes that class of input ("Control.*": the player's buttons).
bool declaresNamed(const std::string& pattern);
// Set a system variable (RemoteContext::ID_*, e.g. ID_BATTERY_LEVEL) for this and every
// later document.
void setSystem(int id, float value);
std::string currentName();

// When the showing document next needs a frame, in ms from now: -1 never (static: nothing
// changes until something is set or touched), 0 continuous (it animates: the last frame
// asked for the next at once), 1 due now. From the
// engine's schedule (time variables it listens to, WakeIn), as of the last frame drawn.
// The hook for sleeping between frames.
int nextFrameMs();
uint32_t framesDrawn();   // frames drawn since start

// The showing document: its name, the size it lays out at (dp), the render buffer's size
// (pixels), and whether it is fitted (scaled by `scale` to the screen).
struct DocInfo {
    std::string name;
    int dpW = 0, dpH = 0;
    int renderW = 0, renderH = 0;
    bool fit = false;
    float scale = 1;
    size_t bytes = 0;
};
DocInfo docInfo();
// The showing document's bytes, as loaded (shared, not copied).
std::shared_ptr<const std::vector<uint8_t>> docBytes();

// The showing document's named variables (NAMED_VARIABLE): their names, and their values
// now: type "text", "number", "color" (#AARRGGBB), "integer" or "other". namedValues asks
// the player task, between frames, and waits up to timeoutMs; not from the player task.
struct NamedValue { std::string name, type, value; };
std::vector<std::string> namedVariableNames();
bool namedValues(std::vector<NamedValue>& out, int timeoutMs);

// What happened on the device, newest last, for a host to poll: "tap" and "long_press" (x, y
// in the document's dp), "action" (a document's hostAction: name = id, value = metadata),
// "named_action" (HostNamedAction), "document" (one was loaded: name), and whatever the app
// adds ("button": value +1 / -1). The last 32 are kept.
struct Event {
    uint32_t seq = 0;   // set by pushEvent: 1, 2, 3 ...
    int64_t atUs = 0;   // esp_timer time, set by pushEvent
    std::string type;
    float x = 0, y = 0;
    std::string name, value;
};
void pushEvent(Event e);
// The events after `since` (0: all kept); *latest = the newest seq, for the next call.
std::vector<Event> events(uint32_t since, uint32_t* latest);

// The touch screen's last contact: where (document dp and panel pixels), whether a finger
// is still down, when (esp_timer us; 0 = never), and how many presses so far.
struct TouchInfo {
    bool down = false;
    float x = 0, y = 0;
    int panelX = 0, panelY = 0;
    int64_t atUs = 0;
    uint32_t downs = 0;
};
TouchInfo lastTouch();
// The touch now down is the player's: no long press, and its release is no click.
void swallowTouch();

// The last frame presented (the document and banner as composed, before the panel's own
// conversion), resampled to w x h (0 x 0: the render size): RGB, or grey (quantized to
// board::kGrays levels on e-ink). Blocks until the player task copies it, up to timeoutMs;
// not from the player task.
struct Frame {
    int w = 0, h = 0, channels = 0;
    std::vector<uint8_t> pixels;   // rows top to bottom, `channels` bytes per pixel
};
bool capture(Frame& out, int w, int h, bool gray, int timeoutMs);

// ── Player task only (from Hooks::tick) ──
bool load(const uint8_t* data, size_t size, const char* name, bool fit = false);   // copies; false keeps the old
void setRenderDiv(int div);    // 1 = native 720x1280, 2 = 360x640 upscaled by the PPA
void bench();                  // raw ThorVG throughput, printed
void requestStats();           // log timing on the next frame
std::vector<std::string> namedVariables();   // "name:type" of the current document
const std::vector<uint8_t>& currentBytes();   // the current document, as loaded

}  // namespace rcstage
