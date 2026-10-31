#include "kobentovg/ThorvgPaintContext.h"

#include "rccore/RemoteContext.h"
#include "rccore/Utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

using namespace rccore;

namespace kobentovg {

// Host debugging aid: RC_TVG_TRACE=1 logs every draw call with its key arguments.
#if !defined(ESP_PLATFORM)
#include <cstdio>
#include <cstdlib>
static const bool kTrace = std::getenv("RC_TVG_TRACE") != nullptr;
#define TRACE(...) do { if (kTrace) std::fprintf(stderr, __VA_ARGS__); } while (0)
#else
static constexpr bool kTrace = false;
#define TRACE(...) do {} while (0)
#endif

// ── Fonts ────────────────────────────────────────────────────────────────────────────────

static const char* kFaceNames[] = {"rc-sans", "rc-sans-bold", "rc-mono"};
static bool sFaceLoaded[3] = {false, false, false};

bool ThorvgPaintContext::registerFont(Face face, const uint8_t* data, size_t size) {
    int i = static_cast<int>(face);
    // copy=false: font data must outlive ThorVG; callers pass flash/static data.
    auto r = tvg::Text::load(kFaceNames[i], reinterpret_cast<const char*>(data),
                             static_cast<uint32_t>(size), "ttf", false);
    sFaceLoaded[i] = (r == tvg::Result::Success);
    return sFaceLoaded[i];
}

const char* ThorvgPaintContext::fontName() const {
    int i = 0;
    if (mPaint.fontType == PaintBundle::FONT_TYPE_MONOSPACE && sFaceLoaded[2]) i = 2;
    else if (mPaint.weight >= 600 && sFaceLoaded[1]) i = 1;
    return sFaceLoaded[i] ? kFaceNames[i] : nullptr;
}

// ── Matrix helpers (tvg::Matrix: x' = e11 x + e12 y + e13) ─────────────────────────────

static tvg::Matrix identity() { return {1, 0, 0, 0, 1, 0, 0, 0, 1}; }

static tvg::Matrix mul(const tvg::Matrix& a, const tvg::Matrix& b) {
    return {a.e11 * b.e11 + a.e12 * b.e21 + a.e13 * b.e31,
            a.e11 * b.e12 + a.e12 * b.e22 + a.e13 * b.e32,
            a.e11 * b.e13 + a.e12 * b.e23 + a.e13 * b.e33,
            a.e21 * b.e11 + a.e22 * b.e21 + a.e23 * b.e31,
            a.e21 * b.e12 + a.e22 * b.e22 + a.e23 * b.e32,
            a.e21 * b.e13 + a.e22 * b.e23 + a.e23 * b.e33,
            a.e31 * b.e11 + a.e32 * b.e21 + a.e33 * b.e31,
            a.e31 * b.e12 + a.e32 * b.e22 + a.e33 * b.e32,
            a.e31 * b.e13 + a.e32 * b.e23 + a.e33 * b.e33};
}

static tvg::Matrix translateM(float dx, float dy) { return {1, 0, dx, 0, 1, dy, 0, 0, 1}; }

static bool finite(const tvg::Matrix& m) {
    return std::isfinite(m.e11) && std::isfinite(m.e12) && std::isfinite(m.e13) &&
           std::isfinite(m.e21) && std::isfinite(m.e22) && std::isfinite(m.e23);
}

static uint32_t premul(uint32_t argb) {
    uint32_t a = argb >> 24;
    if (a == 255) return argb;
    uint32_t r = ((argb >> 16) & 0xFF) * a / 255;
    uint32_t g = ((argb >> 8) & 0xFF) * a / 255;
    uint32_t b = (argb & 0xFF) * a / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// ── Colour filters ────────────────────────────────────────────────────────────────────────
//
// Skia's SkColorFilters::Blend(color, mode): the filter colour is the source, what the paint
// would draw the destination, blended with the W3C / Porter-Duff formulas in premultiplied
// floats. Android's PorterDuffColorFilter / BlendModeColorFilter mean the same.

struct Rgba { float r, g, b, a; };   // premultiplied, 0..1

static Rgba toRgba(uint32_t argb, bool premultiplied) {
    float a = (argb >> 24) / 255.0f;
    float k = premultiplied ? 1.0f / 255.0f : a / 255.0f;
    return {((argb >> 16) & 0xFF) * k, ((argb >> 8) & 0xFF) * k, (argb & 0xFF) * k, a};
}

static uint32_t fromRgba(Rgba c, bool premultiplied) {
    auto clamp01 = [](float v) { return v < 0 ? 0.0f : v > 1 ? 1.0f : v; };
    float a = clamp01(c.a);
    float k = premultiplied ? 1.0f : (a > 0 ? 1.0f / a : 0.0f);
    auto ch = [&](float v) { return (uint32_t) (clamp01(v * k) * 255.0f + 0.5f); };
    return ((uint32_t) (a * 255.0f + 0.5f) << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

static float lum(float r, float g, float b) { return 0.3f * r + 0.59f * g + 0.11f * b; }

static void clipColor(float& r, float& g, float& b) {
    float l = lum(r, g, b), n = std::min({r, g, b}), x = std::max({r, g, b});
    if (n < 0) { r = l + (r - l) * l / (l - n); g = l + (g - l) * l / (l - n); b = l + (b - l) * l / (l - n); }
    if (x > 1) { r = l + (r - l) * (1 - l) / (x - l); g = l + (g - l) * (1 - l) / (x - l); b = l + (b - l) * (1 - l) / (x - l); }
}

static void setLum(float& r, float& g, float& b, float l) {
    float d = l - lum(r, g, b);
    r += d; g += d; b += d;
    clipColor(r, g, b);
}

static void setSat(float& r, float& g, float& b, float s) {
    float* c[3] = {&r, &g, &b};
    std::sort(c, c + 3, [](float* x, float* y) { return *x < *y; });
    float& mn = *c[0]; float& md = *c[1]; float& mx = *c[2];
    if (mx > mn) { md = (md - mn) * s / (mx - mn); mx = s; }
    else { md = mx = 0; }
    mn = 0;
}

static Rgba blend(Rgba s, Rgba d, int mode) {
    using PB = PaintBundle;
    const float sa = s.a, da = d.a;
    auto pd = [&](float fs, float fd) {   // Porter-Duff: s * Fs + d * Fd
        return Rgba{s.r * fs + d.r * fd, s.g * fs + d.g * fd, s.b * fs + d.b * fd, sa * fs + da * fd};
    };
    switch (mode) {
        case PB::BLEND_MODE_CLEAR: return {0, 0, 0, 0};
        case PB::BLEND_MODE_SRC: return s;
        case PB::BLEND_MODE_DST: return d;
        case PB::BLEND_MODE_SRC_OVER: return pd(1, 1 - sa);
        case PB::BLEND_MODE_DST_OVER: return pd(1 - da, 1);
        case PB::BLEND_MODE_SRC_IN: return pd(da, 0);
        case PB::BLEND_MODE_DST_IN: return pd(0, sa);
        case PB::BLEND_MODE_SRC_OUT: return pd(1 - da, 0);
        case PB::BLEND_MODE_DST_OUT: return pd(0, 1 - sa);
        case PB::BLEND_MODE_SRC_ATOP: return pd(da, 1 - sa);
        case PB::BLEND_MODE_DST_ATOP: return pd(1 - da, sa);
        case PB::BLEND_MODE_XOR: return pd(1 - da, 1 - sa);
        case PB::BLEND_MODE_PLUS:
        case PB::PORTER_MODE_ADD:
            return {std::min(s.r + d.r, 1.0f), std::min(s.g + d.g, 1.0f), std::min(s.b + d.b, 1.0f),
                    std::min(sa + da, 1.0f)};
        case PB::BLEND_MODE_MODULATE: return {s.r * d.r, s.g * d.g, s.b * d.b, sa * da};
        case PB::BLEND_MODE_SCREEN:
            return {s.r + d.r - s.r * d.r, s.g + d.g - s.g * d.g, s.b + d.b - s.b * d.b, sa + da - sa * da};
        default: break;
    }
    // The rest blend unpremultiplied colours Cs, Cd with B(Cs, Cd), then composite:
    // co = (1 - da) * s + (1 - sa) * d + sa * da * B.
    float cs[3] = {sa > 0 ? s.r / sa : 0, sa > 0 ? s.g / sa : 0, sa > 0 ? s.b / sa : 0};
    float cd[3] = {da > 0 ? d.r / da : 0, da > 0 ? d.g / da : 0, da > 0 ? d.b / da : 0};
    float bl[3];
    auto sep = [&](float (*f)(float, float)) { for (int i = 0; i < 3; i++) bl[i] = f(cs[i], cd[i]); };
    switch (mode) {
        case PB::BLEND_MODE_MULTIPLY: sep([](float a, float b) { return a * b; }); break;
        case PB::BLEND_MODE_OVERLAY:
            sep([](float a, float b) { return b <= 0.5f ? 2 * a * b : 1 - 2 * (1 - a) * (1 - b); }); break;
        case PB::BLEND_MODE_HARD_LIGHT:
            sep([](float a, float b) { return a <= 0.5f ? 2 * a * b : 1 - 2 * (1 - a) * (1 - b); }); break;
        case PB::BLEND_MODE_DARKEN: sep([](float a, float b) { return std::min(a, b); }); break;
        case PB::BLEND_MODE_LIGHTEN: sep([](float a, float b) { return std::max(a, b); }); break;
        case PB::BLEND_MODE_COLOR_DODGE:
            sep([](float a, float b) { return b <= 0 ? 0.0f : a >= 1 ? 1.0f : std::min(1.0f, b / (1 - a)); }); break;
        case PB::BLEND_MODE_COLOR_BURN:
            sep([](float a, float b) { return b >= 1 ? 1.0f : a <= 0 ? 0.0f : 1 - std::min(1.0f, (1 - b) / a); }); break;
        case PB::BLEND_MODE_SOFT_LIGHT:
            sep([](float a, float b) {
                if (a <= 0.5f) return b - (1 - 2 * a) * b * (1 - b);
                float dd = b <= 0.25f ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
                return b + (2 * a - 1) * (dd - b);
            }); break;
        case PB::BLEND_MODE_DIFFERENCE: sep([](float a, float b) { return std::fabs(a - b); }); break;
        case PB::BLEND_MODE_EXCLUSION: sep([](float a, float b) { return a + b - 2 * a * b; }); break;
        case PB::BLEND_MODE_HUE:
        case PB::BLEND_MODE_SATURATION:
        case PB::BLEND_MODE_COLOR:
        case PB::BLEND_MODE_LUMINOSITY: {
            float r, g, b;
            if (mode == PB::BLEND_MODE_HUE) {
                r = cs[0]; g = cs[1]; b = cs[2];
                setSat(r, g, b, std::max({cd[0], cd[1], cd[2]}) - std::min({cd[0], cd[1], cd[2]}));
                setLum(r, g, b, lum(cd[0], cd[1], cd[2]));
            } else if (mode == PB::BLEND_MODE_SATURATION) {
                r = cd[0]; g = cd[1]; b = cd[2];
                setSat(r, g, b, std::max({cs[0], cs[1], cs[2]}) - std::min({cs[0], cs[1], cs[2]}));
                setLum(r, g, b, lum(cd[0], cd[1], cd[2]));
            } else if (mode == PB::BLEND_MODE_COLOR) {
                r = cs[0]; g = cs[1]; b = cs[2];
                setLum(r, g, b, lum(cd[0], cd[1], cd[2]));
            } else {
                r = cd[0]; g = cd[1]; b = cd[2];
                setLum(r, g, b, lum(cs[0], cs[1], cs[2]));
            }
            bl[0] = r; bl[1] = g; bl[2] = b;
            break;
        }
        default: return pd(1, 1 - sa);   // unknown: draw as SrcOver
    }
    const float both = sa * da;
    return {(1 - da) * s.r + (1 - sa) * d.r + both * bl[0], (1 - da) * s.g + (1 - sa) * d.g + both * bl[1],
            (1 - da) * s.b + (1 - sa) * d.b + both * bl[2], sa + da - both};
}

// The filter applied to one colour: unpremultiplied ARGB in and out.
static uint32_t filterArgb(uint32_t argb, uint32_t filter, int mode) {
    return fromRgba(blend(toRgba(filter, false), toRgba(argb, false), mode), false);
}

uint32_t ThorvgPaintContext::paintColor() const {
    return mPaint.hasColorFilter ? filterArgb(mPaint.color, mPaint.colorFilter, mPaint.colorFilterMode)
                                 : mPaint.color;
}

ThorvgPaintContext::Image& ThorvgPaintContext::filtered(Image& img) {
    const uint32_t color = mPaint.colorFilter;
    const int mode = mPaint.colorFilterMode;
    for (auto& f : mFiltered)
        if (f.src == &img && f.color == color && f.mode == mode && f.version == img.version && f.img.w == img.w)
            return f.img;
    // A changed image replaces its old copy; a few images x filters at most otherwise.
    Filtered* slot = nullptr;
    for (auto& f : mFiltered)
        if (f.src == &img && f.color == color && f.mode == mode) slot = &f;
    if (!slot) {
        if (mFiltered.size() >= 16) mFiltered.erase(mFiltered.begin());
        mFiltered.push_back({&img, 0, color, mode, {}});
        slot = &mFiltered.back();
    }
    slot->version = img.version;
    slot->img.w = img.w;
    slot->img.h = img.h;
    slot->img.px.resize(img.px.size());
    const Rgba src = toRgba(color, false);
    uint32_t lastIn = 0, lastOut = fromRgba(blend(src, toRgba(0, true), mode), true);
    for (size_t i = 0; i < img.px.size(); i++) {
        const uint32_t p = img.px[i];
        if (p != lastIn) { lastIn = p; lastOut = fromRgba(blend(src, toRgba(p, true), mode), true); }
        slot->img.px[i] = lastOut;
    }
    return slot->img;
}

// ── Offscreen (DrawToBitmap) ──────────────────────────────────────────────────────────────

struct ThorvgPaintContext::Offscreen {
    int bitmapId = 0;
    std::unique_ptr<tvg::SwCanvas> canvas;
    // Main-canvas state parked while drawing into the bitmap.
    std::vector<tvg::Scene*> containers;
    tvg::Matrix matrix;
    std::vector<SaveEntry> saves;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────────────────────

ThorvgPaintContext::ThorvgPaintContext(RemoteContext& context, uint32_t* pixels, int w, int h)
    : PaintContext(context), mPixels(pixels), mW(w), mH(h), mMatrix(identity()) {
    mCanvas.reset(tvg::SwCanvas::gen());
    mCanvas->target(mPixels, w, w, h, tvg::ColorSpace::ARGB8888);
}

ThorvgPaintContext::~ThorvgPaintContext() {
    if (mCanvas) mCanvas->remove();
    if (mMeasure) tvg::Paint::rel(mMeasure);
}

void ThorvgPaintContext::beginFrame(const tvg::Matrix& base, uint32_t clearArgb) {
    mCanvas->remove();
    mContainers.clear();
    mSaves.clear();
    mPaintCount = 0;
    m3D.~Tvg3D();               // drop last frame's 3D blits but keep nothing else
    new (&m3D) Tvg3D(*this);
    mMeshBuffers.clear();       // last frame's 2D mesh pixels; Pictures that read them are gone
    mMatrix = identity();
    // Clear with a rect: ThorVG's draw(clear) only clears to transparent black. When the
    // colour is opaque the rect alone covers every pixel, so endFrame skips draw(clear) and
    // saves a whole full-screen pass (~26 ms at 720x1280 in the P4's PSRAM).
    mClearOpaque = (clearArgb >> 24) == 0xFF;
    auto bg = tvg::Shape::gen();
    bg->appendRect(0, 0, (float) mW, (float) mH);
    bg->fill((clearArgb >> 16) & 0xFF, (clearArgb >> 8) & 0xFF, clearArgb & 0xFF,
             clearArgb >> 24);
    mCanvas->add(bg);
    mMatrix = base;
}

void ThorvgPaintContext::retarget(uint32_t* pixels) {
    if (pixels == mPixels) return;
    mPixels = pixels;
    mCanvas->target(mPixels, mW, mW, mH, tvg::ColorSpace::ARGB8888);
}

void ThorvgPaintContext::addOverlay(tvg::Paint* p) {
    mCanvas->add(p);
}

void ThorvgPaintContext::endFrame() {
    if (mOffscreen) endDrawToBitmap();
    mCanvas->draw(!mClearOpaque);
    mCanvas->sync();
}

void ThorvgPaintContext::reset() {
    mPaint = PaintState();
    mPaintStack.clear();
}

// ── Scene-graph plumbing ──────────────────────────────────────────────────────────────────

void ThorvgPaintContext::push(tvg::Paint* p) {
    mPushes++;
    if (!finite(mMatrix)) { tvg::Paint::rel(p); return; }
#if !defined(ESP_PLATFORM)
    static const char* only = std::getenv("RC_TVG_ONLY");   // debug: keep one tvg::Type
    if (only && (int) p->type() != std::atoi(only)) { tvg::Paint::rel(p); return; }
#endif
    p->transform(mMatrix);
    TRACE("  push type=%d depth=%zu saves=%zu m=[%g %g %g / %g %g %g]\n", (int) p->type(),
          mContainers.size(), mSaves.size(), mMatrix.e11, mMatrix.e12, mMatrix.e13, mMatrix.e21,
          mMatrix.e22, mMatrix.e23);
    if (mPaint.blend != tvg::BlendMethod::Normal) p->blend(mPaint.blend);
    if (!mContainers.empty()) mContainers.back()->add(p);
    else if (mOffscreen) mOffscreen->canvas->add(p);
    else mCanvas->add(p);
    mPaintCount++;
}

void ThorvgPaintContext::pushContainer(tvg::Scene* scene) {
    mPushes++;
    // The scene itself stays untransformed: every child carries the full CTM, and so does
    // any clipper attached to the scene.
    if (!mContainers.empty()) mContainers.back()->add(scene);
    else if (mOffscreen) mOffscreen->canvas->add(scene);
    else mCanvas->add(scene);
    mContainers.push_back(scene);
}

tvg::Fill* ThorvgPaintContext::makeFill() const {
    const Gradient& g = mPaint.gradient;
    tvg::Fill* f = nullptr;
    if (g.type == PaintBundle::LINEAR_GRADIENT) {
        auto lg = tvg::LinearGradient::gen();
        lg->linear(g.p[0], g.p[1], g.p[2], g.p[3]);
        f = lg;
    } else if (g.type == PaintBundle::RADIAL_GRADIENT) {
        auto rg = tvg::RadialGradient::gen();
        rg->radial(g.p[0], g.p[1], g.p[2], g.p[0], g.p[1], 0);
        f = rg;
    } else if (g.type == PaintBundle::SWEEP_GRADIENT) {
        // RC/Android SweepGradient: full turn clockwise from +x (3 o'clock) about (cx, cy).
        auto cg = tvg::ConicGradient::gen();
        cg->conic(g.p[0], g.p[1], 0.0f);
        f = cg;
    } else {
        return nullptr;
    }
    if (mPaint.hasColorFilter) {
        std::vector<tvg::Fill::ColorStop> stops = g.stops;
        for (auto& c : stops) {
            uint32_t v = filterArgb((uint32_t(c.a) << 24) | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b,
                                    mPaint.colorFilter, mPaint.colorFilterMode);
            c.a = v >> 24; c.r = (v >> 16) & 0xFF; c.g = (v >> 8) & 0xFF; c.b = v & 0xFF;
        }
        f->colorStops(stops.data(), static_cast<uint32_t>(stops.size()));
    } else {
        f->colorStops(g.stops.data(), static_cast<uint32_t>(g.stops.size()));
    }
    f->spread(g.spread);
    if (g.hasMatrix) f->transform(g.matrix);
    return f;
}

tvg::Shape* ThorvgPaintContext::styled(tvg::Shape* s, bool forceStroke) {
    const PaintState& p = mPaint;
    const uint32_t pc = paintColor();
    uint8_t a = pc >> 24, r = (pc >> 16) & 0xFF, g = (pc >> 8) & 0xFF, b = pc & 0xFF;
    bool gradient = p.gradient.type == PaintBundle::LINEAR_GRADIENT ||
                    p.gradient.type == PaintBundle::RADIAL_GRADIENT ||
                    p.gradient.type == PaintBundle::SWEEP_GRADIENT;
    bool fill = !forceStroke && (p.style == PaintBundle::STYLE_FILL ||
                                 p.style == PaintBundle::STYLE_FILL_AND_STROKE);
    bool stroke = forceStroke || p.style == PaintBundle::STYLE_STROKE ||
                  p.style == PaintBundle::STYLE_FILL_AND_STROKE;
    TRACE("  styled style=%d color=%08x grad=%d fill=%d stroke=%d sw=%g\n", p.style, p.color,
          p.gradient.type, fill, stroke, p.strokeWidth);
    if (gradient) s->opacity(p.color >> 24);    // the paint's alpha scales the whole gradient
    if (fill) {
        if (gradient) s->fill(makeFill());
        else s->fill(r, g, b, a);
    }
    if (stroke) {
        // Skia/Android width 0 is a 1-pixel hairline at any CTM. The shape is pushed with the
        // full CTM applied, so a 1-unit stroke would scale with it; divide by the CTM's scale so
        // it lands at ~1 device pixel. Exact for uniform scales (the viewport-fit case where the
        // 1-unit error is grossest), approximate under shear/non-uniform scale.
        float sw = p.strokeWidth;
        if (!(sw > 0)) {
            float det = std::fabs(mMatrix.e11 * mMatrix.e22 - mMatrix.e12 * mMatrix.e21);
            float scale = det > 0 ? std::sqrt(det) : 1.0f;
            sw = scale > 0 ? 1.0f / scale : 1.0f;
        }
        s->strokeWidth(sw);
        if (gradient) s->strokeFill(makeFill());
        else s->strokeFill(r, g, b, a);
        s->strokeCap(p.cap);
        s->strokeJoin(p.join);
        s->strokeMiterlimit(p.miter);
        if (!p.dash.empty())
            s->strokeDash(p.dash.data(), static_cast<uint32_t>(p.dash.size()), p.dashPhase);
    }
    return s;
}

void ThorvgPaintContext::drawShape(tvg::Shape* s, bool forceStroke) {
    push(styled(s, forceStroke));
}

// ── Geometry ──────────────────────────────────────────────────────────────────────────────

void ThorvgPaintContext::drawRect(float l, float t, float r, float b) {
    auto s = tvg::Shape::gen();
    s->appendRect(l, t, r - l, b - t);
    drawShape(s);
}

void ThorvgPaintContext::drawCircle(float cx, float cy, float radius) {
    auto s = tvg::Shape::gen();
    s->appendCircle(cx, cy, radius, radius);
    drawShape(s);
}

void ThorvgPaintContext::drawLine(float x1, float y1, float x2, float y2) {
    // Skia strokes a line whatever the paint style.
    auto s = tvg::Shape::gen();
    s->moveTo(x1, y1);
    s->lineTo(x2, y2);
    drawShape(s, true);
}

void ThorvgPaintContext::drawOval(float l, float t, float r, float b) {
    auto s = tvg::Shape::gen();
    s->appendCircle((l + r) * 0.5f, (t + b) * 0.5f, (r - l) * 0.5f, (b - t) * 0.5f);
    drawShape(s);
}

void ThorvgPaintContext::drawRoundRect(float l, float t, float r, float b, float rx, float ry) {
    auto s = tvg::Shape::gen();
    s->appendRect(l, t, r - l, b - t, rx, ry);
    drawShape(s);
}

// Append an elliptical arc as cubic segments of at most 90 degrees.
void ThorvgPaintContext::appendArc(tvg::Shape* s, float l, float t, float r, float b,
                                   float startDeg, float sweepDeg, bool moveFirst) {
    const float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
    const float rx = (r - l) * 0.5f, ry = (b - t) * 0.5f;
    sweepDeg = std::max(-360.0f, std::min(360.0f, sweepDeg));
    int n = std::max(1, (int) std::ceil(std::fabs(sweepDeg) / 90.0f));
    float a0 = startDeg * (float) M_PI / 180.0f;
    float step = sweepDeg * (float) M_PI / 180.0f / n;
    float k = 4.0f / 3.0f * std::tan(step / 4.0f);
    float x0 = cx + rx * std::cos(a0), y0 = cy + ry * std::sin(a0);
    if (moveFirst) s->moveTo(x0, y0);
    else s->lineTo(x0, y0);
    for (int i = 0; i < n; i++) {
        float a1 = a0 + step;
        float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        s->cubicTo(cx + rx * (c0 - k * s0), cy + ry * (s0 + k * c0),
                   cx + rx * (c1 + k * s1), cy + ry * (s1 - k * c1),
                   cx + rx * c1, cy + ry * s1);
        a0 = a1;
    }
}

void ThorvgPaintContext::drawArc(float l, float t, float r, float b, float start, float sweep) {
    auto s = tvg::Shape::gen();
    appendArc(s, l, t, r, b, start, sweep, true);
    drawShape(s);
}

void ThorvgPaintContext::drawSector(float l, float t, float r, float b, float start,
                                    float sweep) {
    auto s = tvg::Shape::gen();
    s->moveTo((l + r) * 0.5f, (t + b) * 0.5f);
    appendArc(s, l, t, r, b, start, sweep, false);
    s->close();
    drawShape(s);
}

// Path command ids, NaN-encoded in the float stream (same as the Skia backend).
static constexpr int PATH_MOVE = 10, PATH_LINE = 11, PATH_QUADRATIC = 12, PATH_CONIC = 13,
                     PATH_CUBIC = 14, PATH_CLOSE = 15, PATH_DONE = 16;

static int nanId(float v) {
    int32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits & 0x3FFFFF;
}

void ThorvgPaintContext::buildPathFromFloats(tvg::Shape* s, const std::vector<float>& data) {
    auto val = [&](float v) {
        if (!std::isnan(v)) return v;
        int id = nanId(v);
        if (id >= PATH_MOVE && id <= PATH_DONE) return v;
        return mContext.getFloat(id);
    };
    const int n = static_cast<int>(data.size());
    float lx = 0, ly = 0;   // current point, for quad -> cubic elevation
    int i = 0;
    while (i < n) {
        if (!std::isnan(data[i])) { i++; continue; }
        int cmd = nanId(data[i]);
        switch (cmd) {
            case PATH_MOVE:
                i++;
                if (i + 1 < n) { lx = val(data[i]); ly = val(data[i + 1]); s->moveTo(lx, ly); i += 2; }
                break;
            case PATH_LINE:
                i += 3;
                if (i + 1 < n) { lx = val(data[i]); ly = val(data[i + 1]); s->lineTo(lx, ly); i += 2; }
                break;
            case PATH_QUADRATIC:
            case PATH_CONIC: {
                i += 3;
                int need = cmd == PATH_CONIC ? 5 : 4;
                if (i + need - 1 < n) {
                    float x1 = val(data[i]), y1 = val(data[i + 1]);
                    float x2 = val(data[i + 2]), y2 = val(data[i + 3]);
                    float w = cmd == PATH_CONIC ? val(data[i + 4]) : 1.0f;
                    // Rational quadratic -> cubic; exact for w = 1, close for arc conics.
                    float k = 4.0f / 3.0f * w / (1.0f + w);
                    s->cubicTo(lx + k * (x1 - lx), ly + k * (y1 - ly),
                               x2 + k * (x1 - x2), y2 + k * (y1 - y2), x2, y2);
                    lx = x2; ly = y2;
                    i += need;
                }
                break;
            }
            case PATH_CUBIC:
                i += 3;
                if (i + 5 < n) {
                    float x3 = val(data[i + 4]), y3 = val(data[i + 5]);
                    s->cubicTo(val(data[i]), val(data[i + 1]), val(data[i + 2]),
                               val(data[i + 3]), x3, y3);
                    lx = x3; ly = y3;
                    i += 6;
                }
                break;
            case PATH_CLOSE: i++; s->close(); break;
            case PATH_DONE: i = n; break;
            default: i++; break;
        }
    }
}

bool ThorvgPaintContext::buildPath(tvg::Shape* s, int pathId) {
    auto it = mPaths.find(pathId);
    if (it == mPaths.end()) return false;
    buildPathFromFloats(s, it->second.floats);
    // 1 = even-odd; inverse fills (2, 3) are not supported by ThorVG.
    s->fillRule(it->second.winding == 1 || it->second.winding == 2 ? tvg::FillRule::EvenOdd
                                                                    : tvg::FillRule::NonZero);
    return true;
}

void ThorvgPaintContext::drawPath(int pathId, float start, float end) {
    auto s = tvg::Shape::gen();
    if (!buildPath(s, pathId)) { tvg::Paint::rel(s); return; }
    if (start > 0.0f || end < 1.0f) s->trimpath(start, end, false);
    drawShape(s);
}

// Path tweening interpolates point-by-point between two paths of identical structure,
// matching SkPath::interpolate's precondition.
static bool interpolateFloats(const std::vector<float>& a, const std::vector<float>& b,
                              float t, std::vector<float>& out) {
    if (a.size() != b.size()) return false;
    out.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) {
        if (std::isnan(a[i]) || std::isnan(b[i])) out[i] = a[i];
        else out[i] = a[i] + (b[i] - a[i]) * t;
    }
    return true;
}

void ThorvgPaintContext::drawTweenPath(int path1Id, int path2Id, float tween, float start,
                                       float end) {
    auto p1 = mPaths.find(path1Id), p2 = mPaths.find(path2Id);
    const PathData* src = p1 != mPaths.end() ? &p1->second
                        : p2 != mPaths.end() ? &p2->second : nullptr;
    if (!src) return;
    std::vector<float> mixed;
    const std::vector<float>* use = &src->floats;
    if (p1 != mPaths.end() && p2 != mPaths.end()) {
        if (tween >= 1.0f) use = &p2->second.floats;
        else if (tween > 0.0f && interpolateFloats(p1->second.floats, p2->second.floats, tween, mixed))
            use = &mixed;
    }
    auto s = tvg::Shape::gen();
    buildPathFromFloats(s, *use);
    if (start > 0.0f || end < 1.0f) s->trimpath(start, end, false);
    drawShape(s);
}

void ThorvgPaintContext::tweenPath(int outId, int pathId1, int pathId2, float tween) {
    auto p1 = mPaths.find(pathId1), p2 = mPaths.find(pathId2);
    if (p1 == mPaths.end() || p2 == mPaths.end()) return;
    float t = std::max(0.0f, std::min(1.0f, tween));
    PathData out;
    out.winding = p1->second.winding;
    if (t <= 0.0f) out.floats = p1->second.floats;
    else if (t >= 1.0f) out.floats = p2->second.floats;
    else if (!interpolateFloats(p1->second.floats, p2->second.floats, t, out.floats))
        out.floats = p1->second.floats;
    mPaths[outId] = std::move(out);
}

// ── Bitmaps ──────────────────────────────────────────────────────────────────────────────

ThorvgPaintContext::Image* ThorvgPaintContext::imageFor(int id) {
    auto it = mImages.find(id);
    return it == mImages.end() || it->second.px.empty() ? nullptr : &it->second;
}

void ThorvgPaintContext::drawImage(Image& source, float dl, float dt, float dw, float dh) {
    Image& img = mPaint.hasColorFilter ? filtered(source) : source;
    auto pic = tvg::Picture::gen();
    // copy=false: the pixels live in mImages until the frame is rendered.
    if (pic->load(img.px.data(), img.w, img.h, tvg::ColorSpace::ARGB8888, false) !=
        tvg::Result::Success) {
        tvg::Paint::rel(pic);
        return;
    }
    if (mPaint.nearestFilter) pic->filter(tvg::FilterMethod::Nearest);
    pic->size(dw, dh);
    tvg::Matrix saved = mMatrix;
    mMatrix = mul(mMatrix, translateM(dl, dt));
    uint8_t a = mPaint.color >> 24;
    if (a != 255) pic->opacity(a);
    push(pic);
    mMatrix = saved;
}

void ThorvgPaintContext::drawBitmap(int imageId, float l, float t, float r, float b) {
    if (Image* img = imageFor(imageId)) drawImage(*img, l, t, r - l, b - t);
}

void ThorvgPaintContext::drawBitmapInt(int imageId, int srcL, int srcT, int srcR, int srcB,
                                       int dstL, int dstT, int dstR, int dstB, int) {
    Image* img = imageFor(imageId);
    if (!img || srcR <= srcL || srcB <= srcT) return;
    float sx = float(dstR - dstL) / float(srcR - srcL);
    float sy = float(dstB - dstT) / float(srcB - srcT);
    // Scale the whole image so the source rect lands on the destination, then clip.
    matrixSave();
    clipRect((float) dstL, (float) dstT, (float) dstR, (float) dstB);
    drawImage(*img, dstL - srcL * sx, dstT - srcT * sy, img->w * sx, img->h * sy);
    matrixRestore();
}

// Rasterize an encoded image (PNG or JPEG) into a premultiplied ARGB buffer.
static bool decodeImage(const std::vector<uint8_t>& data, ThorvgPaintContext* /*unused*/,
                        int& w, int& h, std::vector<uint32_t>& px) {
    // The bytes say what they are: Android and Skia decode any codec whatever the declared
    // type, and documents carry JPEG photos under TYPE_PNG.
    const bool jpeg = data.size() > 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
    auto pic = tvg::Picture::gen();
    if (pic->load(reinterpret_cast<const char*>(data.data()), (uint32_t) data.size(),
                  jpeg ? "jpg" : "png", nullptr, true) != tvg::Result::Success) {
        tvg::Paint::rel(pic);
        return false;
    }
    float fw = 0, fh = 0;
    pic->size(&fw, &fh);
    w = (int) fw; h = (int) fh;
    if (w <= 0 || h <= 0) { tvg::Paint::rel(pic); return false; }
    px.assign((size_t) w * h, 0);
    std::unique_ptr<tvg::SwCanvas> c(tvg::SwCanvas::gen());
    c->target(px.data(), w, w, h, tvg::ColorSpace::ARGB8888);
    c->add(pic);
    c->draw(true);
    c->sync();
    return true;
}

void ThorvgPaintContext::loadBitmap(int imageId, int widthAndType, int heightAndEncoding,
                                    const std::vector<uint8_t>& data) {
    int width = widthAndType, type = TYPE_PNG_8888;
    if (widthAndType > 0xFFFF) { type = (widthAndType >> 16) & 0xFFFF; width = widthAndType & 0xFFFF; }
    int height = heightAndEncoding, encoding = ENCODING_INLINE;
    if (heightAndEncoding > 0xFFFF) {
        encoding = (heightAndEncoding >> 16) & 0xFFFF;
        height = heightAndEncoding & 0xFFFF;
    }
    if (encoding == ENCODING_EMPTY) {
        Image& img = mImages[imageId];
        if (img.px.empty() && width > 0 && height > 0) {
            img.w = width; img.h = height;
            img.px.assign((size_t) width * height, 0);
        }
        return;
    }
    if (encoding != ENCODING_INLINE || data.empty()) return;

    // The data op re-runs every paint pass: skip bytes that already produced this image.
    size_t hash = std::hash<std::string>{}(
        std::string(reinterpret_cast<const char*>(data.data()), std::min<size_t>(data.size(), 4096)));
    hash ^= data.size() * 0x9E3779B97F4A7C15ull;
    auto prev = mImageSourceHash.find(imageId);
    if (prev != mImageSourceHash.end() && prev->second == hash && mImages.count(imageId)) return;

    Image img;
    switch (type) {
        case TYPE_PNG_8888:
        case TYPE_PNG:
        case TYPE_PNG_ALPHA_8:
            if (!decodeImage(data, this, img.w, img.h, img.px)) return;
            break;
        case TYPE_RAW8888: {
            size_t n = (size_t) width * height;
            if (data.size() < n * 4) return;
            img.w = width; img.h = height; img.px.resize(n);
            for (size_t i = 0; i < n; i++) {
                const uint8_t* p = &data[i * 4];
                img.px[i] = premul((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                                   (uint32_t(p[2]) << 8) | p[3]);
            }
            break;
        }
        case TYPE_RAW8: {
            size_t n = (size_t) width * height;
            if (data.size() < n) return;
            img.w = width; img.h = height; img.px.resize(n);
            for (size_t i = 0; i < n; i++) {
                uint32_t v = data[i];
                img.px[i] = premul((v << 24) | (v << 16) | (v << 8) | v);
            }
            break;
        }
        default:
            return;
    }
    img.version = ++mImageVersion;
    mImages[imageId] = std::move(img);
    mImageSourceHash[imageId] = hash;
}

void ThorvgPaintContext::beginDrawToBitmap(int bitmapId, int mode, int color) {
    Image* img = imageFor(bitmapId);
    if (!img) return;
    if (mOffscreen) endDrawToBitmap();
    auto off = std::make_unique<Offscreen>();
    off->bitmapId = bitmapId;
    off->containers.swap(mContainers);
    off->matrix = mMatrix;
    off->saves.swap(mSaves);
    if ((mode & 1) == 0) std::fill(img->px.begin(), img->px.end(), premul((uint32_t) color));
    off->canvas.reset(tvg::SwCanvas::gen());
    off->canvas->target(img->px.data(), img->w, img->w, img->h, tvg::ColorSpace::ARGB8888);
    mMatrix = identity();
    mOffscreen = std::move(off);
}

void ThorvgPaintContext::endDrawToBitmap() {
    if (!mOffscreen) return;
    auto off = std::move(mOffscreen);
    off->canvas->draw(false);   // keep existing pixels: the clear was applied explicitly
    off->canvas->sync();
    if (Image* img = imageFor(off->bitmapId)) img->version = ++mImageVersion;
    off->canvas->remove();
    mContainers.swap(off->containers);
    mSaves.swap(off->saves);
    mMatrix = off->matrix;
}

// ── Text ─────────────────────────────────────────────────────────────────────────────────

static int utf8Len(unsigned char c) {
    return c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
}

// Java char (UTF-16 unit) index -> UTF-8 byte offset.
static size_t utf16ToUtf8(const std::string& s, int idx) {
    size_t off = 0;
    int units = 0;
    while (off < s.size() && units < idx) {
        int n = utf8Len((unsigned char) s[off]);
        units += n == 4 ? 2 : 1;
        off += n;
    }
    return std::min(off, s.size());
}

// ThorVG's Text::size() is in points at 96 DPI (it scales by 4/3); RemoteCompose sizes are
// pixels per em, like Skia's. Measured: Noto Sans ascent 1.069em reads 142.5 at size 100.
static constexpr float kPxToTvgSize = 0.75f;

// One off-canvas Text object used only for metrics queries.
tvg::Text* ThorvgPaintContext::measurer(const char* font, float size) {
    if (!mMeasure) mMeasure = tvg::Text::gen();
    if (font != mMeasureFont) { mMeasure->font(font); mMeasureFont = font; }
    if (size != mMeasureSize) { mMeasure->size(size * kPxToTvgSize); mMeasureSize = size; }
    return mMeasure;
}

float ThorvgPaintContext::advanceOf(const std::string& utf8, float size) {
    const char* font = fontName();
    if (!font || utf8.empty() || !(size > 0)) return 0;
    uint64_t key = std::hash<std::string>{}(utf8);
    uint32_t szBits;
    std::memcpy(&szBits, &size, 4);
    key = (key * 1099511628211ull) ^ szBits ^ ((uint64_t) (uintptr_t) font << 32);
    auto hit = mWidthCache.find(key);
    if (hit != mWidthCache.end()) return hit->second;

    tvg::Text* t = measurer(font, size);
    float adv = 0;
    const char* p = utf8.c_str();
    const char* end = p + utf8.size();
    while (p < end) {
        tvg::GlyphMetrics gm{};
        const char* next = nullptr;
        if (t->metrics(p, gm, &next) == tvg::Result::Success) adv += gm.advance;
        p = (next && next > p) ? next : p + utf8Len((unsigned char) *p);
    }
    if (mWidthCache.size() >= 4096) mWidthCache.clear();
    mWidthCache.emplace(key, adv);
    return adv;
}

void ThorvgPaintContext::textMetrics(float size, float& ascent, float& descent) {
    ascent = size * 0.93f;      // fallbacks close to Roboto
    descent = size * 0.24f;
    const char* font = fontName();
    if (!font || !(size > 0)) return;
    tvg::Text* t = measurer(font, size);
    tvg::TextMetrics m{};
    if (t->metrics(m) == tvg::Result::Success) {
        ascent = m.ascent;
        descent = -m.descent;
    }
}

// Draw `utf8` with its baseline origin at (x, baselineY) in the current space.
void ThorvgPaintContext::drawText(const std::string& utf8, float x, float baselineY) {
    const char* font = fontName();
    float size = mPaint.textSize;
    if (!font || utf8.empty() || !(size > 0) || size > 10000) return;
    if (!std::isfinite(x) || !std::isfinite(baselineY)) return;
    TRACE("  drawText '%s' at %g,%g color=%08x grad=%d\n", utf8.c_str(), x, baselineY,
          mPaint.color, mPaint.gradient.type);
    float ascent, descent;
    textMetrics(size, ascent, descent);
    auto t = tvg::Text::gen();
    t->font(font);
    t->size(size * kPxToTvgSize);
    t->text(utf8.c_str());
    if (mPaint.italic) t->italic();
    uint32_t c = paintColor();
    const int gt = mPaint.gradient.type;
    bool gradient = gt == PaintBundle::LINEAR_GRADIENT || gt == PaintBundle::RADIAL_GRADIENT ||
                    gt == PaintBundle::SWEEP_GRADIENT;
    if (mPaint.style == PaintBundle::STYLE_STROKE)
        t->outline(mPaint.strokeWidth > 0 ? mPaint.strokeWidth : 1, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    else if (gradient) {
        // ThorVG applies a Text fill in font units (the glyphs carry an internal scale), so
        // a gradient can't be given to the text directly. Instead fill the text box with the
        // gradient, in draw space like Skia's shader, and mask it by the text's alpha.
        t->fill(255, 255, 255);
        tvg::Matrix saved = mMatrix;
        mMatrix = mul(mMatrix, translateM(x, baselineY - ascent));
        t->transform(mMatrix);
        mMatrix = saved;
        float w = advanceOf(utf8, size);
        float pad = size * 0.5f;   // italics, overhangs
        auto box = tvg::Shape::gen();
        box->appendRect(x - pad, baselineY - ascent - pad, w + 2 * pad, ascent + descent + 2 * pad);
        box->fill(makeFill());
        if ((mPaint.color >> 24) != 255) box->opacity(mPaint.color >> 24);
        box->mask(t, tvg::MaskMethod::Alpha);
        push(box);
        return;
    } else
        t->fill((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    if ((c >> 24) != 255) t->opacity(c >> 24);
    // ThorVG anchors a text box at its top-left (align 0,0); the box top is the ascent line.
    tvg::Matrix saved = mMatrix;
    mMatrix = mul(mMatrix, translateM(x, baselineY - ascent));
    push(t);
    mMatrix = saved;
}

void ThorvgPaintContext::drawTextRun(int textId, int start, int end, int, int, float x, float y,
                                     bool) {
    auto it = mTexts.find(textId);
    if (it == mTexts.end() || start < 0 || start >= end) return;
    const std::string& s = it->second;
    size_t b0 = utf16ToUtf8(s, start), b1 = utf16ToUtf8(s, end);
    if (b0 >= b1) return;
    drawText(s.substr(b0, b1 - b0), x, y);
}

void ThorvgPaintContext::drawTextAnchored(int textId, float x, float y, float panX, float panY,
                                          int flags) {
    auto it = mTexts.find(textId);
    TRACE("textAnchored id=%d found=%d '%s' x=%g y=%g pan=%g,%g flags=%d size=%g\n", textId,
          it != mTexts.end(), it != mTexts.end() ? it->second.c_str() : "", x, y, panX, panY,
          flags, mPaint.textSize);
    if (it == mTexts.end()) return;
    const std::string& text = it->second;
    float ascent, descent;
    textMetrics(mPaint.textSize, ascent, descent);
    float w = advanceOf(text, mPaint.textSize);
    float h = ascent + descent;
    float hOffset = -w * (1.0f + panX) / 2.0f;
    float vOffset = 0;
    if (!std::isnan(panY)) {
        bool baseline = (flags & 8) != 0;
        vOffset = -h * (1.0f - panY) / 2.0f + (baseline ? h / 2.0f : ascent);
    }
    drawText(text, x + hOffset, y + vOffset);
}

// One glyph of a text-on-path run. boxToWorld maps the glyph's text box (top-left at 0,0,
// baseline at y=ascent) all the way to device pixels, so it carries the per-glyph rotation.
// Gradient fills on a path are not supported (rare); the glyph falls back to the solid colour.
void ThorvgPaintContext::drawGlyphTransformed(const std::string& glyph,
                                              const tvg::Matrix& boxToWorld) {
    const char* font = fontName();
    float size = mPaint.textSize;
    if (!font || glyph.empty() || !(size > 0) || size > 10000) return;
    auto t = tvg::Text::gen();
    t->font(font);
    t->size(size * kPxToTvgSize);
    t->text(glyph.c_str());
    if (mPaint.italic) t->italic();
    uint32_t c = paintColor();
    if (mPaint.style == PaintBundle::STYLE_STROKE)
        t->outline(mPaint.strokeWidth > 0 ? mPaint.strokeWidth : 1,
                   (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    else
        t->fill((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    if ((c >> 24) != 255) t->opacity(c >> 24);
    tvg::Matrix saved = mMatrix;
    mMatrix = boxToWorld;
    push(t);
    mMatrix = saved;
}

void ThorvgPaintContext::drawTextOnPath(int textId, int pathId, float hOffset, float vOffset) {
    auto t = mTexts.find(textId);
    auto p = mPaths.find(pathId);
    if (t == mTexts.end() || p == mPaths.end()) return;
    const std::string& text = t->second;
    const char* font = fontName();
    float size = mPaint.textSize;
    if (text.empty() || !font || !(size > 0)) return;

    // Flatten the stored path into a polyline — RC's own path measure (Mesh2D.h), which knows
    // the NaN-tagged verb stream and its padding — resolving any NaN variable ids to values.
    std::vector<float> poly;
    RemoteContext& ctx = getContext();
    int points = mesh2d::flattenPath(p->second.floats, poly, [&](float f) -> float {
        if (!std::isnan(f)) return f;
        int id = mesh2d::pathNanId(f);
        if (id >= mesh2d::PATH_MOVE && id <= mesh2d::PATH_DONE) return f;
        return ctx.getFloat(id);
    });
    if (points < 2) return;

    float total = 0.0f;
    for (int i = 0; i + 1 < points; i++) {
        float dx = poly[(i + 1) * 2] - poly[i * 2];
        float dy = poly[(i + 1) * 2 + 1] - poly[i * 2 + 1];
        total += std::sqrt(dx * dx + dy * dy);
    }
    if (total <= 0.0f) return;

    float ascent, descent;
    textMetrics(size, ascent, descent);

    // Place each glyph centred on its midpoint along the arc, rotated to the tangent and
    // offset perpendicular by vOffset. Mirrors the Skia reference (SkRSXform per glyph);
    // kerning is not applied (the whole backend measures glyph-by-glyph).
    float distance = hOffset;
    const char* s = text.c_str();
    const char* end = s + text.size();
    while (s < end) {
        int gl = (int) utf8Len((unsigned char) *s);
        if (gl <= 0 || s + gl > end) break;
        std::string glyph(s, (size_t) gl);
        s += gl;
        float adv = advanceOf(glyph, size);
        float half = adv * 0.5f;
        float sample[4];
        mesh2d::samplePolyline(poly, points, (distance + half) / total, sample);
        distance += adv;
        if (adv <= 0.0f) continue;
        float px = sample[0], py = sample[1];
        float tx = sample[2], ty = sample[3];    // unit tangent
        px += -ty * vOffset;                      // left normal = (-ty, tx)
        py +=  tx * vOffset;
        // box-local (bx,by) -> user: R*((bx,by) - (half, ascent)) + (px,py), R rotating local
        // +x onto the tangent. The baseline midpoint (half, ascent) lands on the path point.
        tvg::Matrix glyphAffine = {tx, -ty, px - tx * half + ty * ascent,
                                   ty,  tx, py - ty * half - tx * ascent,
                                   0,   0,  1};
        drawGlyphTransformed(glyph, mul(mMatrix, glyphAffine));
    }
}

// ── 2D vertex meshes ───────────────────────────────────────────────────────────────────────
//
// The engine (AddMesh2D) expands layout/expression/spline meshes into a triangle list in user
// space; this backend rasterizes them with rccore's fixed-point triangle rasterizer (the same
// code the software 3D path uses) into a device-space buffer, then composites that buffer as a
// Picture so the current clip and paint order both apply. ThorVG has no drawVertices, which is
// what the Skia backend uses.

void ThorvgPaintContext::setMesh(int meshId, int layout, int uCount, int vCount,
                                 const std::vector<float>& verts, const std::vector<float>& uv,
                                 const std::vector<int32_t>& colors,
                                 const std::vector<int32_t>& indices) {
    Mesh2DEntry e;
    e.layout = layout;
    e.uCount = uCount;
    e.vCount = vCount;
    e.verts = verts;
    if (uv.size() == verts.size()) e.uv = uv;
    if (colors.size() == verts.size() / 2) e.colors = colors;
    e.indices = indices;
    mMeshes2D[meshId] = std::move(e);
}

void ThorvgPaintContext::drawMesh(int meshId, int blend, int imageId) {
    auto it = mMeshes2D.find(meshId);
    if (it == mMeshes2D.end()) return;
    const Mesh2DEntry& e = it->second;
    size_t vn = e.verts.size() / 2;
    if (vn == 0 || e.indices.size() < 3 || !finite(mMatrix)) return;

    // Map every vertex to device pixels through the current CTM and find the footprint.
    std::vector<float> dev(vn * 2);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (size_t i = 0; i < vn; i++) {
        float x = e.verts[i * 2], y = e.verts[i * 2 + 1];
        float dx = mMatrix.e11 * x + mMatrix.e12 * y + mMatrix.e13;
        float dy = mMatrix.e21 * x + mMatrix.e22 * y + mMatrix.e23;
        dev[i * 2] = dx; dev[i * 2 + 1] = dy;
        minX = std::min(minX, dx); maxX = std::max(maxX, dx);
        minY = std::min(minY, dy); maxY = std::max(maxY, dy);
    }
    if (!std::isfinite(minX) || !std::isfinite(maxX)) return;
    int ox = std::max(0, (int) std::floor(minX));
    int oy = std::max(0, (int) std::floor(minY));
    int ex = std::min(mW, (int) std::ceil(maxX));
    int ey = std::min(mH, (int) std::ceil(maxY));
    int bw = ex - ox, bh = ey - oy;
    if (bw <= 0 || bh <= 0) return;

    // One buffer per mesh, in device-space ARGB8888S (straight), the layout the rasterizer and
    // ThorVG's ARGB8888S Picture both expect. Sized to the footprint so a small mesh costs a
    // small allocation, not a full screen (this runs on a 4 MB-PSRAM board).
    std::vector<uint32_t> buf((size_t) bw * bh, 0u);
    std::vector<float> zbuf((size_t) bw * bh);
    d3::clearDepth(zbuf.data(), bw * bh);
    int32_t* img = reinterpret_cast<int32_t*>(buf.data());

    const Image* tex = nullptr;
    if (blend == mesh2d::BLEND_MODULATE && imageId != mesh2d::NO_IMAGE && !e.uv.empty())
        tex = imageFor(imageId);

    int32_t fallback = (int32_t) paintColor();   // vertices with no per-vertex colour
    int tris = (int) (e.indices.size() / 3);
    for (int tcnt = 0; tcnt < tris; tcnt++) {
        int i0 = e.indices[tcnt * 3], i1 = e.indices[tcnt * 3 + 1], i2 = e.indices[tcnt * 3 + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 ||
            (size_t) i0 >= vn || (size_t) i1 >= vn || (size_t) i2 >= vn) continue;
        // A flat, strictly-decreasing z makes later triangles win the rasterizer's `>` depth
        // test, so overlapping triangles composite in index (painter's) order like drawVertices.
        float z = -(float) tcnt;
        float x0 = dev[i0 * 2] - ox, y0 = dev[i0 * 2 + 1] - oy;
        float x1 = dev[i1 * 2] - ox, y1 = dev[i1 * 2 + 1] - oy;
        float x2 = dev[i2 * 2] - ox, y2 = dev[i2 * 2 + 1] - oy;
        int32_t c0 = e.colors.empty() ? fallback : e.colors[i0];
        int32_t c1 = e.colors.empty() ? fallback : e.colors[i1];
        int32_t c2 = e.colors.empty() ? fallback : e.colors[i2];
        if (tex) {
            // rccore's textured fill flips v for the GL convention; RC 2D meshes don't flip, so
            // pass 1-v to cancel it. iw = 1: an affine 2D mesh has no perspective.
            d3::fillTriangleTextured(zbuf.data(), img,
                reinterpret_cast<const int32_t*>(tex->px.data()), tex->w, tex->h,
                c2, c1, c0, bw, bh,
                x2, y2, z, e.uv[i2 * 2], 1.0f - e.uv[i2 * 2 + 1], 1.0f,
                x1, y1, z, e.uv[i1 * 2], 1.0f - e.uv[i1 * 2 + 1], 1.0f,
                x0, y0, z, e.uv[i0 * 2], 1.0f - e.uv[i0 * 2 + 1], 1.0f);
        } else {
            d3::fillTriangleGouraud(zbuf.data(), img, c2, c1, c0, bw, bh,
                                    x2, y2, z, x1, y1, z, x0, y0, z);
        }
    }

    auto pic = tvg::Picture::gen();
    // copy=false: the pixels live in mMeshBuffers until the frame renders. std::move keeps the
    // vector's data pointer, so the pointer handed to ThorVG stays valid after the push_back.
    if (pic->load(buf.data(), bw, bh, tvg::ColorSpace::ARGB8888S, false) != tvg::Result::Success) {
        tvg::Paint::rel(pic);
        return;
    }
    mMeshBuffers.push_back(std::move(buf));
    tvg::Matrix saved = mMatrix;
    mMatrix = translateM((float) ox, (float) oy);   // buffer is already device-space
    uint8_t a = mPaint.color >> 24;
    if (a != 255) pic->opacity(a);
    push(pic);
    mMatrix = saved;
}

void ThorvgPaintContext::matrixFromMesh(int meshId, float u, float v, int flags) {
    auto it = mMeshes2D.find(meshId);
    if (it == mMeshes2D.end()) return;
    const Mesh2DEntry& e = it->second;
    if (e.verts.empty()) return;
    float frame[6];
    if (!mesh2d::sampleFrame(e.layout, e.uCount, e.vCount, e.verts.data(),
                             (int) (e.verts.size() / 2), u, v, frame)) {
        return;
    }
    float m[6];   // duX, duY, dvX, dvY, originX, originY
    mesh2d::buildMatrix(frame, flags, m);
    // The affine maps the (u,v) basis: x' = duX*x + dvX*y + originX, so du is the first column.
    tvg::Matrix affine = {m[0], m[2], m[4], m[1], m[3], m[5], 0, 0, 1};
    mMatrix = mul(mMatrix, affine);
}

float ThorvgPaintContext::measureTextWidth(const std::string& text, float fontSize) {
    return advanceOf(text, fontSize);
}

float ThorvgPaintContext::measureTextHeight(const std::string&, float fontSize) {
    float a, d;
    textMetrics(fontSize, a, d);
    return a + d;
}

float ThorvgPaintContext::measureTextAscent(const std::string&, float fontSize) {
    float a, d;
    textMetrics(fontSize, a, d);
    return a;
}

// RC/Java text indices are UTF-16 code units, but our strings are UTF-8. Convert a UTF-16
// index to a byte offset in `s` (clamped to [0, s.size()]). Without this, a non-ASCII prefix
// shifts every measurement because one glyph is several bytes.
static size_t utf16IndexToByte(const std::string& s, int utf16Index) {
    if (utf16Index <= 0) return 0;
    const size_t n = s.size();
    size_t bytes = 0;
    int units = 0;
    while (bytes < n && units < utf16Index) {
        unsigned char c = (unsigned char) s[bytes];
        size_t len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3
                   : ((c >> 3) == 0x1E) ? 4 : 1;
        if (bytes + len > n) len = n - bytes;
        units += (len == 4) ? 2 : 1;   // a 4-byte codepoint is a UTF-16 surrogate pair
        bytes += len;
    }
    return bytes;
}

void ThorvgPaintContext::getTextBounds(int textId, int start, int end, int, float bounds[4]) {
    auto it = mTexts.find(textId);
    if (it == mTexts.end()) { bounds[0] = bounds[1] = bounds[2] = bounds[3] = 0; return; }
    const std::string& text = it->second;
    size_t sB = utf16IndexToByte(text, start);
    size_t eB = end >= 0 ? utf16IndexToByte(text, end) : text.size();
    std::string sub = sB < eB ? text.substr(sB, eB - sB) : std::string();
    float a, d;
    textMetrics(mPaint.textSize, a, d);
    bounds[0] = 0;
    bounds[1] = -a;
    bounds[2] = advanceOf(sub, mPaint.textSize);
    bounds[3] = d;
}

// ── Paint state ──────────────────────────────────────────────────────────────────────────

static tvg::BlendMethod toBlend(int mode) {
    switch (mode) {
        case PaintBundle::BLEND_MODE_MULTIPLY:
        case PaintBundle::BLEND_MODE_MODULATE: return tvg::BlendMethod::Multiply;
        case PaintBundle::BLEND_MODE_SCREEN: return tvg::BlendMethod::Screen;
        case PaintBundle::BLEND_MODE_OVERLAY: return tvg::BlendMethod::Overlay;
        case PaintBundle::BLEND_MODE_DARKEN: return tvg::BlendMethod::Darken;
        case PaintBundle::BLEND_MODE_LIGHTEN: return tvg::BlendMethod::Lighten;
        case PaintBundle::BLEND_MODE_COLOR_DODGE: return tvg::BlendMethod::ColorDodge;
        case PaintBundle::BLEND_MODE_COLOR_BURN: return tvg::BlendMethod::ColorBurn;
        case PaintBundle::BLEND_MODE_HARD_LIGHT: return tvg::BlendMethod::HardLight;
        case PaintBundle::BLEND_MODE_SOFT_LIGHT: return tvg::BlendMethod::SoftLight;
        case PaintBundle::BLEND_MODE_DIFFERENCE: return tvg::BlendMethod::Difference;
        case PaintBundle::BLEND_MODE_EXCLUSION: return tvg::BlendMethod::Exclusion;
        case PaintBundle::BLEND_MODE_HUE: return tvg::BlendMethod::Hue;
        case PaintBundle::BLEND_MODE_SATURATION: return tvg::BlendMethod::Saturation;
        case PaintBundle::BLEND_MODE_COLOR: return tvg::BlendMethod::Color;
        case PaintBundle::BLEND_MODE_LUMINOSITY: return tvg::BlendMethod::Luminosity;
        case PaintBundle::BLEND_MODE_PLUS:
        case PaintBundle::PORTER_MODE_ADD: return tvg::BlendMethod::Add;
        default: return tvg::BlendMethod::Normal;   // SrcOver and the unsupported Porter-Duff modes
    }
}

void ThorvgPaintContext::applyPaint(const PaintBundle& bundle) {
    // A bundle is a delta over the current paint (see SkiaPaintContext::applyPaintBundle).
    const auto& arr = bundle.getData();
    const int len = bundle.size();
    auto f = [&](int idx) {
        float v = PaintBundle::intBitsToFloat(arr[idx]);
        return std::isnan(v) ? mContext.getFloat(Utils::idFromNan(v)) : v;
    };
    int i = 0;
    while (i < len) {
        int32_t cmd = arr[i++];
        int tag = cmd & 0xFFFF;
        int upper = (cmd >> 16) & 0xFFFF;
        switch (tag) {
            case PaintBundle::TEXT_SIZE: mPaint.textSize = f(i++); break;
            case PaintBundle::COLOR:
            case PaintBundle::COLOR_ID:
                // Like SkPaint::setColor: the shader (gradient) stays and keeps winning; only
                // the alpha of this colour still modulates it.
                mPaint.color = (uint32_t) arr[i++];
                break;
            case PaintBundle::STROKE_WIDTH: mPaint.strokeWidth = f(i++); break;
            case PaintBundle::STROKE_MITER: mPaint.miter = f(i++); break;
            case PaintBundle::STROKE_CAP:
                mPaint.cap = upper == 1 ? tvg::StrokeCap::Round
                           : upper == 2 ? tvg::StrokeCap::Square : tvg::StrokeCap::Butt;
                break;
            case PaintBundle::STYLE: mPaint.style = upper; break;
            case PaintBundle::GRADIENT: {
                TRACE("gradient type=%d\n", upper);
                Gradient g;
                g.type = upper;
                int numColors = arr[i++] & 0xFF;
                std::vector<uint32_t> colors(numColors);
                for (int j = 0; j < numColors; j++) colors[j] = (uint32_t) arr[i++];
                int numStops = arr[i++];
                std::vector<float> stops(numStops);
                for (int j = 0; j < numStops; j++) stops[j] = PaintBundle::intBitsToFloat(arr[i++]);
                for (int j = 0; j < numColors; j++) {
                    float off = (numStops == numColors) ? stops[j]
                              : (numColors > 1 ? float(j) / (numColors - 1) : 0.0f);
                    // Skia (and so Android) pins stop positions into [0, 1]; ThorVG would
                    // honour e.g. [0, 2] literally and only reach the midpoint colour.
                    off = std::max(0.0f, std::min(1.0f, off));
                    uint32_t c = colors[j];
                    g.stops.push_back({off, uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c),
                                       uint8_t(c >> 24)});
                }
                auto spread = [](int tm) {
                    return tm == 1 ? tvg::FillSpread::Repeat
                         : tm == 2 ? tvg::FillSpread::Reflect : tvg::FillSpread::Pad;
                };
                if (upper == PaintBundle::LINEAR_GRADIENT) {
                    for (int k = 0; k < 4; k++) g.p[k] = PaintBundle::intBitsToFloat(arr[i++]);
                    g.spread = spread(arr[i++]);
                } else if (upper == PaintBundle::RADIAL_GRADIENT) {
                    for (int k = 0; k < 3; k++) g.p[k] = PaintBundle::intBitsToFloat(arr[i++]);
                    g.spread = spread(arr[i++]);
                } else if (upper == PaintBundle::SWEEP_GRADIENT) {
                    g.p[0] = PaintBundle::intBitsToFloat(arr[i++]);
                    g.p[1] = PaintBundle::intBitsToFloat(arr[i++]);
                }
                TRACE("  stops=%zu p=%g,%g,%g,%g\n", g.stops.size(), g.p[0], g.p[1], g.p[2], g.p[3]);
                for (auto& st : g.stops) TRACE("   %g: %02x%02x%02x%02x\n", st.offset, st.a, st.r, st.g, st.b);
                mPaint.gradient = std::move(g);
                break;
            }
            case PaintBundle::ALPHA: {
                float a = std::max(0.0f, std::min(1.0f, f(i++)));
                mPaint.color = (mPaint.color & 0x00FFFFFF) | (uint32_t(a * 255.0f + 0.5f) << 24);
                break;
            }
            case PaintBundle::COLOR_FILTER:
            case PaintBundle::COLOR_FILTER_ID:
                mPaint.hasColorFilter = true;
                mPaint.colorFilter = (uint32_t) arr[i++];
                mPaint.colorFilterMode = upper;
                break;
            case PaintBundle::CLEAR_COLOR_FILTER: mPaint.hasColorFilter = false; break;
            case PaintBundle::ANTI_ALIAS: break;
            case PaintBundle::STROKE_JOIN:
                mPaint.join = upper == 1 ? tvg::StrokeJoin::Round
                            : upper == 2 ? tvg::StrokeJoin::Bevel : tvg::StrokeJoin::Miter;
                break;
            case PaintBundle::BLEND_MODE: mPaint.blend = toBlend(upper); break;
            case PaintBundle::TYPEFACE:
                mPaint.weight = (upper & 0x3FF) > 0 ? (upper & 0x3FF) : 400;
                mPaint.italic = (upper >> 10) > 0;
                mPaint.fontType = arr[i++];
                break;
            case PaintBundle::FALLBACK_TYPEFACE: i++; break;
            case PaintBundle::SHADER:
                // Runtime (AGSL) shaders can't run here; a 0 id clears the shader.
                if (arr[i++] == 0) mPaint.gradient.type = -1;
                break;
            case PaintBundle::SHADER_MATRIX: {
                int bits = arr[i++];
                float fl = PaintBundle::intBitsToFloat(bits);
                int matrixId = std::isnan(fl) ? Utils::idFromNan(fl) : bits;
                if (matrixId == 0) break;
                const auto* m = mContext.getObjectMatrix(matrixId);
                if (m && m->size() >= 9) {
                    const auto& v = *m;
                    mPaint.gradient.hasMatrix = true;
                    mPaint.gradient.matrix = {v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]};
                }
                break;
            }
            case PaintBundle::IMAGE_FILTER_QUALITY:
            case PaintBundle::FILTER_BITMAP:
                // Android: filterBitmap(upper != 0); quality 0 (NONE) is nearest. ThorVG offers
                // only nearest vs its default bilinear, so upper == 0 means nearest.
                mPaint.nearestFilter = (upper == 0);
                break;
            case PaintBundle::FONT_AXIS: i += 2 * upper; break;
            case PaintBundle::TEXTURE: i += 3; break;   // bitmap shaders unsupported
            case PaintBundle::PATH_EFFECT: {
                int count = upper;
                mPaint.dash.clear();
                if (count >= 3 && count <= 2028) {
                    int type = arr[i];
                    float phase = PaintBundle::intBitsToFloat(arr[i + 1]);
                    int n = arr[i + 2];
                    std::vector<float> iv;
                    for (int j = 0; j < n && j < count - 3; j++)
                        iv.push_back(PaintBundle::intBitsToFloat(arr[i + 3 + j]));
                    if (type == 1 && iv.size() >= 2 && iv.size() % 2 == 0) {
                        mPaint.dash = std::move(iv);
                        mPaint.dashPhase = phase;
                    }
                }
                i += count;
                break;
            }
            default:
                // Unknown tag: its payload length is unknown, so stop rather than misparse.
                i = len;
                break;
        }
    }
}

void ThorvgPaintContext::savePaint() { mPaintStack.push_back(mPaint); }

void ThorvgPaintContext::restorePaint() {
    if (mPaintStack.empty()) return;
    mPaint = std::move(mPaintStack.back());
    mPaintStack.pop_back();
}

// ── Transform / clip / layers ─────────────────────────────────────────────────────────────

void ThorvgPaintContext::matrixSave() { mSaves.push_back({mMatrix, mContainers.size()}); }

void ThorvgPaintContext::matrixRestore() {
    if (mSaves.empty()) return;
    mMatrix = mSaves.back().matrix;
    mContainers.resize(std::min(mContainers.size(), mSaves.back().containerDepth));
    mSaves.pop_back();
}

void ThorvgPaintContext::matrixScale(float sx, float sy, float cx, float cy) {
    if (!std::isfinite(sx) || !std::isfinite(sy)) return;
    mMatrix = mul(mMatrix, {sx, 0, cx - sx * cx, 0, sy, cy - sy * cy, 0, 0, 1});
}

void ThorvgPaintContext::matrixTranslate(float dx, float dy) {
    mMatrix = mul(mMatrix, translateM(dx, dy));
}

void ThorvgPaintContext::matrixRotate(float degrees, float cx, float cy) {
    float r = degrees * (float) M_PI / 180.0f;
    float c = std::cos(r), s = std::sin(r);
    mMatrix = mul(mMatrix, {c, -s, cx - c * cx + s * cy, s, c, cy - s * cx - c * cy, 0, 0, 1});
}

void ThorvgPaintContext::matrixSkew(float sx, float sy) {
    mMatrix = mul(mMatrix, {1, sx, 0, sy, 1, 0, 0, 0, 1});
}

void ThorvgPaintContext::saveLayerAlpha(float alpha, float, float, float, float) {
    matrixSave();
    auto scene = tvg::Scene::gen();
    scene->opacity((uint8_t) (std::max(0.0f, std::min(1.0f, alpha)) * 255.0f + 0.5f));
    pushContainer(scene);
}

void ThorvgPaintContext::saveLayerWithBlur(float sx, float sy, float, float, float, float) {
    matrixSave();
    auto scene = tvg::Scene::gen();
    // Sigma is in document units; scale by the CTM's x scale to get device pixels.
    float scale = std::sqrt(mMatrix.e11 * mMatrix.e11 + mMatrix.e21 * mMatrix.e21);
    double sigma = std::max(sx, sy) * scale;
    if (sigma > 0) scene->add(tvg::SceneEffect::GaussianBlur, sigma, 0, 0, 75);
    pushContainer(scene);
}

void ThorvgPaintContext::restoreLayer() { matrixRestore(); }

void ThorvgPaintContext::clipRect(float l, float t, float r, float b) {
    TRACE("clipRect %g %g %g %g depth=%zu\n", l, t, r, b, mContainers.size());
    auto clip = tvg::Shape::gen();
    clip->appendRect(l, t, r - l, b - t);
    clip->fill(255, 255, 255, 255);
    clip->transform(mMatrix);
    auto scene = tvg::Scene::gen();
    scene->clip(clip);
    pushContainer(scene);
}

void ThorvgPaintContext::clipPath(int pathId, int) {
    TRACE("clipPath %d depth=%zu\n", pathId, mContainers.size());
    auto clip = tvg::Shape::gen();
    if (!buildPath(clip, pathId)) { tvg::Paint::rel(clip); return; }
    if (kTrace) {
        const tvg::PathCommand* cmds; const tvg::Point* pts; uint32_t nc, np;
        clip->path(&cmds, &nc, &pts, &np);
        TRACE("  clip path cmds=%u pts=%u:", nc, np);
        for (uint32_t k = 0; k < np && k < 40; k++) TRACE(" (%.0f,%.0f)", pts[k].x, pts[k].y);
        TRACE("\n");
    }
    clip->fill(255, 255, 255, 255);
    clip->transform(mMatrix);
    auto scene = tvg::Scene::gen();
    scene->clip(clip);
    pushContainer(scene);
}

void ThorvgPaintContext::roundedClipRect(float w, float h, float topStart, float topEnd,
                                         float bottomStart, float bottomEnd) {
    auto clip = tvg::Shape::gen();
    // Per-corner radii: TL, TR, BR, BL.
    float tl = topStart, tr = topEnd, br = bottomEnd, bl = bottomStart;
    const float k = 0.5522847f;
    clip->moveTo(tl, 0);
    clip->lineTo(w - tr, 0);
    if (tr > 0) clip->cubicTo(w - tr + tr * k, 0, w, tr - tr * k, w, tr);
    clip->lineTo(w, h - br);
    if (br > 0) clip->cubicTo(w, h - br + br * k, w - br + br * k, h, w - br, h);
    clip->lineTo(bl, h);
    if (bl > 0) clip->cubicTo(bl - bl * k, h, 0, h - bl + bl * k, 0, h - bl);
    clip->lineTo(0, tl);
    if (tl > 0) clip->cubicTo(0, tl - tl * k, tl - tl * k, 0, tl, 0);
    clip->close();
    clip->fill(255, 255, 255, 255);
    clip->transform(mMatrix);
    auto scene = tvg::Scene::gen();
    scene->clip(clip);
    pushContainer(scene);
}

// ── Data ─────────────────────────────────────────────────────────────────────────────────

void ThorvgPaintContext::loadText(int id, const std::string& text) { mTexts[id] = text; }

std::string ThorvgPaintContext::getText(int id) {
    auto it = mTexts.find(id);
    return it != mTexts.end() ? it->second : std::string();
}

void ThorvgPaintContext::loadPathData(int instanceId, int winding, const std::vector<float>& path) {
    mPaths[instanceId] = {path, winding};
}

void ThorvgPaintContext::appendPathData(int instanceId, const std::vector<float>& path) {
    auto& p = mPaths[instanceId];
    p.floats.insert(p.floats.end(), path.begin(), path.end());
}

// ── 3D ───────────────────────────────────────────────────────────────────────────────────

d3::SoftwarePaint3DContext* ThorvgPaintContext::Tvg3D::ensure() {
    int w = (int) std::lround(mOwner.getContext().mWidth);
    int h = (int) std::lround(mOwner.getContext().mHeight);
    if (w <= 0 || h <= 0) { w = mOwner.mW; h = mOwner.mH; }
    if (w <= 0 || h <= 0) return nullptr;
    if (!mSized || mCtx.width() != w || mCtx.height() != h) {
        mCtx.setSize(w, h);
        mCtx.clearDepth3D();
        mSized = true;
    }
    return &mCtx;
}

void ThorvgPaintContext::Tvg3D::blit() {
    const auto& px = mCtx.colorBuffer();
    int w = mCtx.width(), h = mCtx.height();
    if (w <= 0 || h <= 0 || (int) px.size() < w * h) return;
    const size_t bytes = (size_t) w * h * sizeof(uint32_t);
    const tvg::Matrix& m = mOwner.mMatrix;
    // Nothing pushed since the last snapshot, under the same transform: the 3D buffer already
    // holds that mesh plus this one, so refresh the snapshot in place. One full-screen copy per
    // run of meshes instead of one per mesh matters on a 4 MB PSRAM board.
    if (!mBlits.empty() && mOwner.mPushes == mLastBlitPush &&
        std::memcmp(&m, &mLastBlitMatrix, sizeof m) == 0) {
        std::memcpy(mBlits.back().get(), px.data(), bytes);
        return;
    }
    // Snapshot: later meshes keep drawing into mCtx, but this frame's picture must not move.
    std::unique_ptr<uint32_t[], FreeDeleter> copy(static_cast<uint32_t*>(std::malloc(bytes)));
    if (!copy) {
        static bool warned = false;
        if (!warned) std::fprintf(stderr, "kobentovg: no memory for a %zu-byte 3D snapshot; skipped\n", bytes);
        warned = true;
        return;
    }
    std::memcpy(copy.get(), px.data(), bytes);
    auto pic = tvg::Picture::gen();
    if (pic->load(copy.get(), w, h, tvg::ColorSpace::ARGB8888S, false) != tvg::Result::Success) {
        tvg::Paint::rel(pic);
        return;
    }
    mBlits.push_back(std::move(copy));
    mOwner.push(pic);
    mLastBlitPush = mOwner.mPushes;
    mLastBlitMatrix = m;
}

void ThorvgPaintContext::Tvg3D::defineMesh3D(int id, const std::vector<int32_t>& indices,
                                             const std::vector<float>& verts,
                                             const std::vector<float>& normals,
                                             const std::vector<float>& uv) {
    if (auto* c = ensure()) c->defineMesh3D(id, indices, verts, normals, uv);
}
void ThorvgPaintContext::Tvg3D::setCamera3D(int projection, const std::vector<float>& pp,
                                            const std::vector<float>& vp) {
    if (auto* c = ensure()) c->setCamera3D(projection, pp, vp);
}
void ThorvgPaintContext::Tvg3D::matrix3Op(int sub, const std::vector<float>& args) {
    if (auto* c = ensure()) c->matrix3Op(sub, args);
}
void ThorvgPaintContext::Tvg3D::drawMesh3D(int meshId, int mode) {
    auto* c = ensure();
    if (!c) return;
    c->setBaseColorArgb((int32_t) mOwner.mPaint.color);
    c->drawMesh3D(meshId, mode);
    blit();
}
void ThorvgPaintContext::Tvg3D::clearDepth3D() {
    if (auto* c = ensure()) c->clearDepth3D();
}
void ThorvgPaintContext::Tvg3D::setLights3D(const std::vector<int>& types,
                                            const std::vector<int32_t>& colors,
                                            const std::vector<float>& params) {
    if (auto* c = ensure()) c->setLights3D(types, colors, params);
}
void ThorvgPaintContext::Tvg3D::setTexture3D(int bitmapId) {
    auto* c = ensure();
    if (!c) return;
    Image* img = mOwner.imageFor(bitmapId);
    if (!img) { c->setTextureData({}, 0, 0); return; }
    std::vector<int32_t> px(img->px.begin(), img->px.end());
    c->setTextureData(px, img->w, img->h);
}
void ThorvgPaintContext::Tvg3D::setMaterial3D(float s, float sh) {
    if (auto* c = ensure()) c->setMaterial3D(s, sh);
}
void ThorvgPaintContext::Tvg3D::setDepthBias3D(float k, float s) {
    if (auto* c = ensure()) c->setDepthBias3D(k, s);
}

}  // namespace kobentovg
