#include "player.hpp"
#include "display.hpp"

#include "rccore/CoreDocument.h"
#include "rccore/RemoteContext.h"
#include "rccore/WireBuffer.h"
#include "rccore/operations/DrawOperations.h"   // NamedVariable
#include "kobentovg/ThorvgPaintContext.h"        // tvg::Matrix

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace kobento {

using rccore::RemoteContext;

static constexpr int kFitDefault = 600;   // size-less documents lay out on a 600x600 canvas

static int64_t wallMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

Player::Player(int renderW, int renderH, float density, int theme)
    : mRenderW(renderW), mRenderH(renderH), mTheme(theme), mDensity(density),
      mPx((size_t) renderW * renderH, 0) {}

Player::~Player() = default;

void Player::collectVars() {
    mVars.clear();
    std::function<void(const std::vector<std::unique_ptr<rccore::Operation>>&)> walk =
        [&](const std::vector<std::unique_ptr<rccore::Operation>>& ops) {
            for (auto& op : ops) {
                if (auto* nv = dynamic_cast<const rccore::NamedVariable*>(op.get())) mVars.push_back(nv);
                if (!op->mChildren.empty()) walk(op->mChildren);
            }
        };
    walk(mDoc->getOperations());
}

// Apply one override to the current document if it declares the name. Types: 0 string, 1 float,
// 2 color (#AARRGGBB or RRGGBB), 4 integer — the same set rcstage/rc2png handle.
static void applyNamed(RemoteContext& ctx, const std::vector<const rccore::NamedVariable*>& vars,
                       const std::string& n, const std::string& v) {
    for (auto* nv : vars) {
        if (nv->varName != n) continue;
        if (nv->varType == 0) ctx.overrideText(nv->varId, v);
        else if (nv->varType == 1) ctx.overrideFloat(nv->varId, (float) atof(v.c_str()));
        else if (nv->varType == 2) {
            const char* h = v.c_str() + (!v.empty() && v[0] == '#');
            uint32_t c = (uint32_t) strtoul(h, nullptr, 16);
            if (strlen(h) <= 6) c |= 0xFF000000u;
            ctx.overrideColor(nv->varId, (int) c);
        } else if (nv->varType == 4) ctx.overrideInteger(nv->varId, atoi(v.c_str()));
    }
}

void Player::layoutAndApply() {
    rccore::WireBuffer buf(mBytes.data(), mBytes.size());
    mDoc = std::make_unique<rccore::CoreDocument>();
    mDoc->initFromBuffer(buf);   // caller validated parse before committing bytes

    mCtx = std::make_unique<RemoteContext>();
    mPc = std::make_unique<kobentovg::ThorvgPaintContext>(*mCtx, mPx.data(), mRenderW, mRenderH);
    mCtx->setPaintContext(mPc.get());
    mCtx->setDocument(mDoc.get());
    mCtx->hostAction = [this](int id, const std::string& md) {
        if (id != 0 || !onHostAction) return;
        size_t eq = md.find('=');
        onHostAction(md.substr(0, eq), eq == std::string::npos ? std::string() : md.substr(eq + 1));
    };
    mCtx->hostNamedAction = [this](const std::string& n, int, const std::string& v) {
        if (onHostAction) onHostAction(n, v);
    };

    // Player mode: the document lays out into the surface, in dp. Fit: at its own header size,
    // scaled to sit centred on black.
    const int sw = mRenderW, sh = mRenderH;
    // Rasterizing at 1/div of the panel must not change the layout, only the pixels per dp.
    const float density = mDensity / mDiv;
    if (!mFit) {
        mDpW = (int) std::lround(sw / density);
        mDpH = (int) std::lround(sh / density);
        mScale = density;
        mOx = mOy = 0;
    } else {
        mDpW = mDoc->getWidth() > 0 ? mDoc->getWidth() : kFitDefault;
        mDpH = mDoc->getHeight() > 0 ? mDoc->getHeight() : kFitDefault;
        mScale = std::min((float) sw / mDpW, (float) sh / mDpH);
        mOx = (sw - mDpW * mScale) * 0.5f;
        mOy = (sh - mDpH * mScale) * 0.5f;
    }
    mDoc->setWidth(mDpW);
    mDoc->setHeight(mDpH);
    mCtx->mWidth = (float) mDpW;
    mCtx->mHeight = (float) mDpH;
    mCtx->loadFloat(RemoteContext::ID_WINDOW_WIDTH, (float) mDpW);
    mCtx->loadFloat(RemoteContext::ID_WINDOW_HEIGHT, (float) mDpH);
    mCtx->loadFloat(RemoteContext::ID_DENSITY, density);
    mDoc->registerListeners(*mCtx);
    for (auto& [id, v] : mStickySystem) mCtx->loadFloat(id, v);
    mDoc->applyDataOperations(*mCtx, mTheme);
    collectVars();
    for (auto& [n, v] : mStickyNamed) applyNamed(*mCtx, mVars, n, v);
}

bool Player::load(std::vector<uint8_t> bytes, std::string name, bool fit) {
    rccore::WireBuffer buf(bytes.data(), bytes.size());
    rccore::CoreDocument probe;
    if (!probe.initFromBuffer(buf)) return false;   // keep the current document
    mBytes = std::move(bytes);
    mName = std::move(name);
    mFit = fit;
    layoutAndApply();
    mFrames = 0;
    return true;
}

double Player::renderFrame() {
    static int64_t sLast = 0;
    const tvg::Matrix base{mScale, 0, mOx, 0, mScale, mOy, 0, 0, 1};
    // Keep the touch position live each frame while a gesture is held (touch expressions read it).
    if (mPointerActive) {
        mCtx->loadFloat(RemoteContext::ID_TOUCH_POS_X, mTouchDocX);
        mCtx->loadFloat(RemoteContext::ID_TOUCH_POS_Y, mTouchDocY);
    }
    auto t0 = std::chrono::steady_clock::now();
    mPc->beginFrame(base, mFit ? 0xFF000000u : 0xFFFFFFFFu);
    mDoc->paint(*mCtx, mTheme);
    mPc->endFrame();
    mLastFrameMs = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0).count();
    const int64_t now = wallMs();
    if (sLast) mLastDelta = (double) (now - sLast);
    sLast = now;
    mFrames++;
    return mLastFrameMs;
}

int Player::nextDelayMs() {
    const int delay = mCtx->getRepaintDelay(wallMs());
    if (mPc->doesNeedsRepaint()) return 0;
    return delay;
}

void Player::setNamed(std::string name, std::string value) {
    if (mCtx) applyNamed(*mCtx, mVars, name, value);
    mStickyNamed[std::move(name)] = std::move(value);
}

void Player::setSystem(int id, float value) {
    mStickySystem[id] = value;
    if (mCtx) mCtx->loadFloat(id, value);
}

// Pointer input. bufX/bufY are render-buffer px; the base matrix maps document coords (dp) to
// buffer px as px = dp*scale + offset, so the inverse recovers document coords. The engine wants
// document coords for hit-testing (it sets mWidth/mHeight to the layout size, not the surface).
void Player::pointerDown(float bufX, float bufY) {
    if (!mDoc || !mCtx) return;
    mPointerActive = true;
    mTouchDocX = (bufX - mOx) / mScale;
    mTouchDocY = (bufY - mOy) / mScale;
    mCtx->loadFloat(RemoteContext::ID_TOUCH_POS_X, mTouchDocX);
    mCtx->loadFloat(RemoteContext::ID_TOUCH_POS_Y, mTouchDocY);
    mDoc->touchDown(*mCtx, mTouchDocX, mTouchDocY);
}

void Player::pointerMove(float bufX, float bufY) {
    if (!mDoc || !mCtx || !mPointerActive) return;
    mTouchDocX = (bufX - mOx) / mScale;
    mTouchDocY = (bufY - mOy) / mScale;
    mCtx->loadFloat(RemoteContext::ID_TOUCH_POS_X, mTouchDocX);
    mCtx->loadFloat(RemoteContext::ID_TOUCH_POS_Y, mTouchDocY);
    mDoc->touchDrag(*mCtx, mTouchDocX, mTouchDocY);
}

void Player::pointerUp(float bufX, float bufY, float velX, float velY) {
    if (!mDoc || !mCtx) return;
    const float x = (bufX - mOx) / mScale, y = (bufY - mOy) / mScale;
    mCtx->loadFloat(RemoteContext::ID_TOUCH_POS_X, x);
    mCtx->loadFloat(RemoteContext::ID_TOUCH_POS_Y, y);
    mDoc->touchUp(*mCtx, x, y, velX, velY);   // velocity drives flings (window px/sec)
    mDoc->onClick(*mCtx, x, y);               // click areas -> host actions
    mPointerActive = false;
}

std::vector<Player::NamedValue> Player::namedValues() const {
    std::vector<NamedValue> out;
    for (auto* nv : mVars) {
        NamedValue nvv;
        nvv.name = nv->varName;
        char buf[64];
        switch (nv->varType) {
            case 0: nvv.type = "text";   nvv.value = mCtx->getText(nv->varId); break;
            case 1: nvv.type = "number"; snprintf(buf, sizeof buf, "%g", mCtx->getFloat(nv->varId)); nvv.value = buf; break;
            case 2: nvv.type = "color";  snprintf(buf, sizeof buf, "#%08X", (unsigned) mCtx->getColor(nv->varId)); nvv.value = buf; break;
            case 4: nvv.type = "integer"; nvv.value = std::to_string(mCtx->getInteger(nv->varId)); break;
            default: nvv.type = "other"; break;
        }
        out.push_back(std::move(nvv));
    }
    return out;
}

void Player::setRenderSize(int w, int h) {
    mDiv = 1;
    mRenderW = std::max(1, w);
    mRenderH = std::max(1, h);
    mPx.assign((size_t) mRenderW * mRenderH, 0);
    if (!mBytes.empty()) layoutAndApply();
}

void Player::setRenderDiv(int div, int baseW, int baseH) {
    mDiv = std::max(1, std::min(8, div));
    mRenderW = std::max(1, baseW / mDiv);
    mRenderH = std::max(1, baseH / mDiv);
    mPx.assign((size_t) mRenderW * mRenderH, 0);
    if (!mBytes.empty()) layoutAndApply();
}

bool Player::snap(const std::string& path) const {
    return writePng(path, mPx.data(), mRenderW, mRenderH);
}

void Player::bench(int frames) {
    if (mBytes.empty()) return;
    frames = std::max(1, frames);
    const tvg::Matrix base{mScale, 0, mOx, 0, mScale, mOy, 0, 0, 1};
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; i++) {
        mPc->beginFrame(base, mFit ? 0xFF000000u : 0xFFFFFFFFu);
        mDoc->paint(*mCtx, mTheme);
        mPc->endFrame();
    }
    double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
    printf("bench %-24s %6.2f ms/frame  (%d paints, %dx%d)\n", mName.c_str(),
           ms / frames, (int) paintCount(), mRenderW, mRenderH);
}

void Player::printStats() const {
    const double fps = mLastDelta > 0 ? 1000.0 / mLastDelta : 0;
    printf("%s  %dx%d px (dp %dx%d, scale %.3g%s)  last %.2f ms  %.1f fps  %d paints\n",
           mName.c_str(), mRenderW, mRenderH, mDpW, mDpH, mScale, mFit ? ", fit" : "",
           mLastFrameMs, fps, (int) paintCount());
}

uint32_t Player::paintCount() const { return mPc ? (uint32_t) mPc->paintCount() : 0; }

}  // namespace kobento
