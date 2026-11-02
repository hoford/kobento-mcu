// Where a rendered frame is shown: a desktop window (SDL2), or nowhere (headless).
//
// A frame is premultiplied ARGB8888, `srcW` x `srcH` pixels. The window backend uploads it to a
// texture and scales it to the current window (on the GPU when an accelerated renderer is
// available, else a software blit); it also reports window input — navigation keys, fullscreen
// toggle, resize, close, and pointer (mouse/touch) events — so the player behaves like the
// reference rcviewer. On a host without a display (CI, or over SSH with no session), openWindow()
// returns nullptr and the caller falls back to headless, where present() is a no-op and the
// `snap` command writes a PNG.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kobento {

// High-level input actions, mapped from window keys (mirrors the rcviewer controls).
enum class Input { Next, Prev, Reload, PauseToggle, Snap, Quit, Fullscreen };

// Pointer (mouse / single-finger touch) phase.
enum class Phase { Down, Move, Up };

struct Event {
    enum Kind { Key, Resize, Close, Pointer } kind;
    Input key = Input::Next;       // Kind::Key
    int w = 0, h = 0;              // Kind::Resize (new drawable size, px)
    Phase phase = Phase::Move;     // Kind::Pointer
    float px = 0, py = 0;          // Kind::Pointer (drawable px, top-left origin)
};

// How to open the window. Accelerated asks SDL for a GPU renderer (VideoCore on the Pi) with
// vsync, falling back to a software renderer; linearFilter picks smooth vs. nearest (fast)
// upscaling when the render buffer is smaller than the window.
struct WindowOptions {
    int w = 600, h = 600;
    std::string title = "Kobento RC player";
    bool fullscreen = false;       // start borderless-fullscreen on the current desktop
    bool accelerated = true;       // try a GPU renderer first
    bool linearFilter = true;      // false = nearest-neighbour (cheaper upscale)
};

class Display {
public:
    virtual ~Display() = default;
    virtual int width() const = 0;              // drawable size in px
    virtual int height() const = 0;
    virtual void present(const uint32_t* argb, int srcW, int srcH) = 0;
    virtual void poll(std::vector<Event>& out) {}   // window events since last call
    virtual void setTitle(const std::string&) {}
    virtual void toggleFullscreen() {}
    virtual bool fullscreen() const { return false; }
    virtual const char* name() const = 0;
};

// Open a window on the desktop (Wayland or X11). nullptr if SDL has no video target (no session
// / headless build).
std::unique_ptr<Display> openWindow(const WindowOptions& opt);

// No window: present() does nothing; drive it with the `snap` console command.
std::unique_ptr<Display> openHeadless(int w, int h);

// Premultiplied ARGB8888 -> RGBA PNG. Shared with the `snap` command. false on write error.
bool writePng(const std::string& path, const uint32_t* argb, int w, int h);

}  // namespace kobento
