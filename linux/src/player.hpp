// One RemoteCompose document live in an ARGB8888 buffer.
//
//   rccore (engine) -> kobentovg::ThorvgPaintContext -> this buffer -> Display::present
//
// A trimmed host-side counterpart of components/rcstage, without the FreeRTOS/ESP-IDF plumbing:
// everything here runs on the player thread (the render loop), so there are no locks. It lays a
// document out the way a device does (player mode) or scaled-to-fit on black, feeds the engine's
// repaint schedule back as nextDelayMs(), and keeps sticky named/system variables so overrides
// (system.board, a document's own state) survive a document change.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rccore { class CoreDocument; class RemoteContext; class NamedVariable; }
namespace kobentovg { class ThorvgPaintContext; }

namespace kobento {

class Player {
public:
    // Render at renderW x renderH pixels (the panel size, or 1/div of it). theme: -3 light,
    // -2 dark. density: dp -> px for player-mode layout.
    Player(int renderW, int renderH, float density, int theme);
    ~Player();

    // Replace the document (bytes copied). fit: lay it out at its own header size (else 600x600)
    // and scale to fit on black, rather than at the screen's size. false if it didn't parse —
    // the previous document stays.
    bool load(std::vector<uint8_t> bytes, std::string name, bool fit);

    // Paint one frame into the render buffer. Returns milliseconds spent.
    double renderFrame();
    const uint32_t* pixels() const { return mPx.data(); }
    int renderW() const { return mRenderW; }
    int renderH() const { return mRenderH; }

    // Milliseconds until the document wants its next frame: -1 never (static), 0 continuous.
    int nextDelayMs();

    // Sticky: applied to this document (if it declares the name) and every one loaded later.
    void setNamed(std::string name, std::string value);
    void setSystem(int id, float value);

    struct NamedValue { std::string name, type, value; };
    std::vector<NamedValue> namedValues() const;

    // A document's hostAction(0, "name[=value]") / HostNamedAction(name, value). Called on the
    // player thread during renderFrame(); the handler should defer document changes (enqueue),
    // not reload from inside the callback.
    std::function<void(const std::string& name, const std::string& value)> onHostAction;

    // Pointer (mouse / touch) input, in RENDER-BUFFER pixels (the caller scales window px by
    // renderW()/windowW). Mapped to document coordinates here and dispatched to the engine, which
    // routes to touch listeners and click areas. Velocity on release is in window px/sec (fling
    // physics are tuned to screen pixels, like the reference), passed straight through.
    void pointerDown(float bufX, float bufY);
    void pointerMove(float bufX, float bufY);
    void pointerUp(float bufX, float bufY, float velX, float velY);

    // Render at a new pixel size (e.g. after the window is resized). Reloads the current
    // document so it re-lays-out. Resets the render divisor to 1.
    void setRenderSize(int w, int h);
    // Re-render at 1/div of the given base size (div 1 = native). Reloads to relay out.
    void setRenderDiv(int div, int baseW, int baseH);
    int renderDiv() const { return mDiv; }

    bool snap(const std::string& path) const;
    void bench(int frames);                  // raw throughput of the current document, printed
    void printStats() const;

    const std::string& name() const { return mName; }
    bool fit() const { return mFit; }
    int dpW() const { return mDpW; }
    int dpH() const { return mDpH; }
    uint32_t paintCount() const;

private:
    void layoutAndApply();                   // (re)lay the current bytes out and apply overrides
    void collectVars();

    int mRenderW, mRenderH, mTheme, mDiv = 1;
    float mDensity;
    std::vector<uint32_t> mPx;

    std::unique_ptr<rccore::RemoteContext> mCtx;
    std::unique_ptr<rccore::CoreDocument> mDoc;
    std::unique_ptr<kobentovg::ThorvgPaintContext> mPc;

    std::vector<uint8_t> mBytes;             // the current document, as loaded
    std::string mName;
    bool mFit = false;
    int mDpW = 0, mDpH = 0;
    float mScale = 1, mOx = 0, mOy = 0;

    std::vector<const rccore::NamedVariable*> mVars;   // declared by the current document
    std::map<std::string, std::string> mStickyNamed;
    std::map<int, float> mStickySystem;

    bool mPointerActive = false;                       // a touch/drag is in progress
    float mTouchDocX = 0, mTouchDocY = 0;              // its position, in document coords

    double mLastFrameMs = 0;
    double mLastDelta = 0;                    // wall ms between the last two frames (for fps)
    uint64_t mFrames = 0;
};

}  // namespace kobento
