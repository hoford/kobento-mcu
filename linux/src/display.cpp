#include "display.hpp"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <vector>

#if defined(KOBENTO_HAVE_SDL)
#include <SDL.h>
#endif

namespace kobento {

// ── PNG ──────────────────────────────────────────────────────────────────────────────────

static void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

static void pngChunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> c;
    be32(c, (uint32_t) data.size());
    c.insert(c.end(), type, type + 4);
    c.insert(c.end(), data.begin(), data.end());
    be32(c, (uint32_t) crc32(0, c.data() + 4, (uInt) (c.size() - 4)));
    fwrite(c.data(), 1, c.size(), f);
}

bool writePng(const std::string& path, const uint32_t* px, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t) (w * 4 + 1) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);   // filter: none
        for (int x = 0; x < w; x++) {
            uint32_t p = px[(size_t) y * w + x];
            uint32_t a = p >> 24, r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            if (a && a < 255) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; }
            raw.push_back(r); raw.push_back(g); raw.push_back(b); raw.push_back(a);
        }
    }
    uLongf zlen = compressBound((uLong) raw.size());
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), (uLong) raw.size(), 6) != Z_OK) return false;
    z.resize(zlen);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    be32(ihdr, w); be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});   // 8-bit, RGBA
    pngChunk(f, "IHDR", ihdr);
    pngChunk(f, "IDAT", z);
    pngChunk(f, "IEND", {});
    fclose(f);
    return true;
}

// ── Headless ─────────────────────────────────────────────────────────────────────────────

class HeadlessDisplay : public Display {
public:
    HeadlessDisplay(int w, int h) : mW(w), mH(h) {}
    int width() const override { return mW; }
    int height() const override { return mH; }
    void present(const uint32_t*, int, int) override {}   // nothing to show; use `snap`
    const char* name() const override { return "headless"; }
private:
    int mW, mH;
};

std::unique_ptr<Display> openHeadless(int w, int h) {
    return std::make_unique<HeadlessDisplay>(w, h);
}

// ── SDL2 window (the PIXEL desktop, Wayland or X11) ─────────────────────────────────────────

#if defined(KOBENTO_HAVE_SDL)

class SdlDisplay : public Display {
public:
    ~SdlDisplay() override {
        if (mTex) SDL_DestroyTexture(mTex);
        if (mRen) SDL_DestroyRenderer(mRen);
        if (mWin) SDL_DestroyWindow(mWin);
        SDL_Quit();
    }

    bool open(const WindowOptions& opt) {
        // Deterministic app name, set before video init. SDL2 feeds this into the Wayland app_id
        // (and X11 WM_CLASS), so a compositor window rule (e.g. labwc
        // <windowRule identifier="kobento">) can reliably match this window to force it
        // fullscreen/focused — the SSH-proof way to raise a kiosk window, since a Wayland client
        // cannot raise itself.
        SDL_SetHint(SDL_HINT_APP_NAME, "kobento");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return false;
        }
        // Scale quality for the final upscale blit (render buffer -> window): linear is smooth,
        // nearest is cheaper and is what the reference CPU backend uses. Set before the renderer.
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, opt.linearFilter ? "linear" : "nearest");

        uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
        if (opt.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;   // borderless, keeps the desktop
        mWin = SDL_CreateWindow(opt.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                opt.w, opt.h, flags);
        if (!mWin) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
        mFull = opt.fullscreen;

        // Prefer a GPU renderer (VideoCore on the Pi) with vsync — it uploads the ThorVG frame as
        // a texture and scales it on the GPU, which is the win in fullscreen. Fall back to the
        // software renderer if no accelerated one is available (some headless/remote sessions).
        if (opt.accelerated)
            mRen = SDL_CreateRenderer(mWin, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!mRen) mRen = SDL_CreateRenderer(mWin, -1, SDL_RENDERER_SOFTWARE);
        if (!mRen) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }

        SDL_GetWindowSizeInPixels(mWin, &mW, &mH);
        SDL_RaiseWindow(mWin);     // kiosk: come to the front (esp. when launched over SSH)
        if (mFull) SDL_ShowCursor(SDL_DISABLE);
        SDL_RendererInfo ri;
        const char* kind = (SDL_GetRendererInfo(mRen, &ri) == 0 &&
                            (ri.flags & SDL_RENDERER_ACCELERATED)) ? "gpu" : "software";
        mName = std::string("sdl window (") + SDL_GetCurrentVideoDriver() + ", " + kind + ")";
        return true;
    }

    int width() const override { return mW; }
    int height() const override { return mH; }
    const char* name() const override { return mName.c_str(); }
    void setTitle(const std::string& t) override { if (mWin) SDL_SetWindowTitle(mWin, t.c_str()); }
    bool fullscreen() const override { return mFull; }

    void toggleFullscreen() override {
        mFull = !mFull;
        SDL_SetWindowFullscreen(mWin, mFull ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        SDL_ShowCursor(mFull ? SDL_DISABLE : SDL_ENABLE);
        SDL_RaiseWindow(mWin);
        SDL_GetWindowSizeInPixels(mWin, &mW, &mH);   // a size-changed event also follows
    }

    void present(const uint32_t* argb, int srcW, int srcH) override {
        if (!mTex || srcW != mTexW || srcH != mTexH) {
            if (mTex) SDL_DestroyTexture(mTex);
            mTex = SDL_CreateTexture(mRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     srcW, srcH);
            mTexW = srcW; mTexH = srcH;
        }
        SDL_UpdateTexture(mTex, nullptr, argb, srcW * 4);
        SDL_RenderClear(mRen);
        SDL_RenderCopy(mRen, mTex, nullptr, nullptr);   // scale to fill the window
        SDL_RenderPresent(mRen);
    }

    void poll(std::vector<Event>& out) override {
        // Mouse events are in window points; the drawable may be larger (HiDPI). Scale to px.
        int wpW = 0, wpH = 0; SDL_GetWindowSize(mWin, &wpW, &wpH);
        const float rx = wpW > 0 ? (float) mW / wpW : 1.f;
        const float ry = wpH > 0 ? (float) mH / wpH : 1.f;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { out.push_back({Event::Close}); }
            else if (e.type == SDL_WINDOWEVENT &&
                     (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                      e.window.event == SDL_WINDOWEVENT_RESIZED)) {
                SDL_GetWindowSizeInPixels(mWin, &mW, &mH);
                Event ev{Event::Resize}; ev.w = mW; ev.h = mH;
                out.push_back(ev);
            } else if (e.type == SDL_KEYDOWN) {
                Event ev{Event::Key};
                bool mapped = true;
                switch (e.key.keysym.sym) {
                    case SDLK_RIGHT: case SDLK_DOWN: case SDLK_n: case SDLK_PAGEDOWN: ev.key = Input::Next; break;
                    case SDLK_LEFT: case SDLK_UP: case SDLK_p: case SDLK_PAGEUP: ev.key = Input::Prev; break;
                    case SDLK_r: ev.key = Input::Reload; break;
                    case SDLK_SPACE: ev.key = Input::PauseToggle; break;
                    case SDLK_s: ev.key = Input::Snap; break;
                    case SDLK_f: case SDLK_F11: ev.key = Input::Fullscreen; break;
                    case SDLK_q: case SDLK_ESCAPE: ev.key = Input::Quit; break;
                    default: mapped = false; break;
                }
                if (mapped) out.push_back(ev);
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                mDown = true;
                out.push_back(pointer(Phase::Down, e.button.x * rx, e.button.y * ry));
            } else if (e.type == SDL_MOUSEMOTION && mDown) {
                out.push_back(pointer(Phase::Move, e.motion.x * rx, e.motion.y * ry));
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                mDown = false;
                out.push_back(pointer(Phase::Up, e.button.x * rx, e.button.y * ry));
            } else if (e.type == SDL_FINGERDOWN) {
                out.push_back(pointer(Phase::Down, e.tfinger.x * mW, e.tfinger.y * mH));
            } else if (e.type == SDL_FINGERMOTION) {
                out.push_back(pointer(Phase::Move, e.tfinger.x * mW, e.tfinger.y * mH));
            } else if (e.type == SDL_FINGERUP) {
                out.push_back(pointer(Phase::Up, e.tfinger.x * mW, e.tfinger.y * mH));
            }
        }
    }

private:
    static Event pointer(Phase p, float x, float y) {
        Event ev{Event::Pointer}; ev.phase = p; ev.px = x; ev.py = y; return ev;
    }

    SDL_Window* mWin = nullptr;
    SDL_Renderer* mRen = nullptr;
    SDL_Texture* mTex = nullptr;
    int mW = 0, mH = 0, mTexW = 0, mTexH = 0;
    bool mFull = false, mDown = false;
    std::string mName;
};

std::unique_ptr<Display> openWindow(const WindowOptions& opt) {
    auto d = std::make_unique<SdlDisplay>();
    if (!d->open(opt)) return nullptr;
    return d;
}

#else   // built without SDL

std::unique_ptr<Display> openWindow(const WindowOptions&) { return nullptr; }

#endif

}  // namespace kobento
