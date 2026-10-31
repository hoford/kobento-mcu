#include "rcstage/stage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <atomic>
#include <sys/time.h>
#include <memory>
#include <map>
#include <mutex>
#include <utility>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "sdkconfig.h"

#include "rccore/CoreDocument.h"
#include "rccore/RemoteContext.h"
#include "rccore/WireBuffer.h"
#include "rccore/operations/DrawOperations.h"
#include "board/board.h"
#include "kobentovg/ThorvgPaintContext.h"

static const char* TAG = "rcstage";

extern const uint8_t _binary_Roboto_Regular_ttf_start[], _binary_Roboto_Regular_ttf_end[];
extern const uint8_t _binary_Roboto_Bold_ttf_start[], _binary_Roboto_Bold_ttf_end[];
extern const uint8_t _binary_RobotoMono_Regular_ttf_start[], _binary_RobotoMono_Regular_ttf_end[];

namespace rcstage {

// Documents lay out in dp: the panel's pixels / the board's density (360 x 640 on the Tab5).
static constexpr float kDensity = board::kDensity;
static constexpr int kThemeLight = -3;

static int sRenderDiv = 1;
static Hooks sHooks;

// Sticky values, applied to each newly loaded document (player task only).
static std::map<std::string, std::string> sNamedValues;
static std::map<int, float> sSystemValues;
// Actions a document raised during a touch, run once the engine is done with that touch:
// a hook may load another document, which must not happen under the old one's onClick.
// Player task only.
struct RaisedAction { int idOrType; std::string a, b; bool host; };
static std::vector<RaisedAction> sRaised;
// listenNamed: patterns and their listeners (sListenLock; read by the player task).
static std::mutex sListenLock;
static std::vector<std::pair<std::string, NamedListener>> sListeners;

static bool matches(const std::string& pattern, const std::string& name) {
    if (!pattern.empty() && pattern.back() == '*') return name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    return name == pattern;
}

// A parsed document is thousands of small objects (one per operation, its strings and
// arrays), all kept while it shows. Where small allocations go to internal RAM first
// (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: 16 KB on the Tab5's Muse build), a large document
// would take all of it (the 73 KB start screen took 300 KB), and the radio link and TLS
// need it. So while a document loads, every allocation tries PSRAM first.
struct PsramFirst {
#if CONFIG_SPIRAM_USE_MALLOC
    PsramFirst() { heap_caps_malloc_extmem_enable(0); }
    ~PsramFirst() { heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL); }
#endif
};

// A document's named variables, wherever they are: most sit at the top level, but one a
// canvas declares (an exported canvas variable) is inside the canvas.
static void collectNamed(const std::vector<std::unique_ptr<rccore::Operation>>& ops,
                         std::vector<const rccore::NamedVariable*>& out) {
    for (auto& op : ops) {
        if (auto* v = dynamic_cast<const rccore::NamedVariable*>(op.get())) out.push_back(v);
        if (!op->mChildren.empty()) collectNamed(op->mChildren, out);
    }
}

struct Player {
    // The document must outlive its CoreDocument; shared so other tasks can keep a copy
    // (rc.pin saves it) without copying it.
    std::shared_ptr<const std::vector<uint8_t>> bytes;
    std::string name;
    std::unique_ptr<rccore::CoreDocument> doc;
    std::unique_ptr<rccore::RemoteContext> ctx;
    std::unique_ptr<kobentovg::ThorvgPaintContext> pc;
    int dpW = 0, dpH = 0;         // the size the document lays out at
    int rw = 0, rh = 0;           // render buffer size
    // fit: the document at its own size, scaled by `scale` and offset by (ox, oy) dp to sit
    // centred in the screen's dp. Otherwise scale 1 and no offset.
    bool fit = false;
    float scale = 1, ox = 0, oy = 0;

    bool load(std::vector<uint8_t> data, const char* docName, uint32_t* pixels, bool fitDoc = false) {
        PsramFirst psram;
        auto next = std::make_unique<rccore::CoreDocument>();
        try {
            rccore::WireBuffer buf(data.data(), data.size());
            if (!next->initFromBuffer(buf)) return false;
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "parse threw: %s", e.what());
            return false;
        }
        pc.reset();
        ctx.reset();
        doc = std::move(next);
        bytes = std::make_shared<const std::vector<uint8_t>>(std::move(data));   // same buffer
        name = docName;
        const int winW = (int) (board::kWidth / kDensity), winH = (int) (board::kHeight / kDensity);
        // A document with no size in its header (androidx demos, watch faces) was drawn for a
        // phone-sized canvas in pixels: fit it from 600 x 600, the default of rc2png
        // --native and of the Skia reference renderer.
        constexpr int kFitDefault = 600;
        fit = fitDoc;
        dpW = !fit ? winW : doc->getWidth() > 0 ? doc->getWidth() : kFitDefault;
        dpH = !fit ? winH : doc->getHeight() > 0 ? doc->getHeight() : kFitDefault;
        scale = fit ? std::min((float) winW / dpW, (float) winH / dpH) : 1.0f;
        ox = fit ? (winW - dpW * scale) / 2 : 0;
        oy = fit ? (winH - dpH * scale) / 2 : 0;
        doc->setWidth(dpW);
        doc->setHeight(dpH);
        ctx = std::make_unique<rccore::RemoteContext>();
        rw = board::kWidth / sRenderDiv;
        rh = board::kHeight / sRenderDiv;
        pc = std::make_unique<kobentovg::ThorvgPaintContext>(*ctx, pixels, rw, rh);
        ctx->setPaintContext(pc.get());
        ctx->setDocument(doc.get());
        ctx->mWidth = (float) dpW;
        ctx->mHeight = (float) dpH;
        doc->registerListeners(*ctx);
        ctx->loadFloat(rccore::RemoteContext::ID_WINDOW_WIDTH, (float) dpW);
        ctx->loadFloat(rccore::RemoteContext::ID_WINDOW_HEIGHT, (float) dpH);
        for (auto& [id, v] : sSystemValues) ctx->loadFloat(id, v);
        ctx->hostAction = [](int id, const std::string& md) {
            ESP_LOGI(TAG, "hostAction(%d, \"%s\")", id, md.c_str());
            sRaised.push_back({id, md, {}, true});
            pushEvent({0, 0, "action", 0, 0, std::to_string(id), md});
        };
        ctx->hostNamedAction = [](const std::string& n, int type, const std::string& v) {
            ESP_LOGI(TAG, "namedAction %s = %s", n.c_str(), v.c_str());
            sRaised.push_back({type, n, v, false});
            pushEvent({0, 0, "named_action", 0, 0, n, v});
        };
        watched.clear();   // the old document's
        vars.clear();
        collectNamed(doc->getOperations(), vars);
        doc->applyDataOperations(*ctx, kThemeLight);
        for (auto& [n, v] : sNamedValues) setNamed(n, v);
        ownsBanner = named("system.banner_title") != nullptr;
        // The variables someone listens to, and their values now: changes from here on are
        // the document's.
        {
            std::lock_guard<std::mutex> g(sListenLock);
            for (auto* v : vars)
                for (auto& [pattern, fn] : sListeners)
                    if (matches(pattern, v->varName)) { watched.push_back({v, valueOf(*v)}); break; }
        }
        return true;
    }

    struct Watched { const rccore::NamedVariable* var; std::string last; };
    std::vector<Watched> watched;

    // What a touch changed goes to the listeners once the gesture is over: `gesture` from the
    // finger down until kSettleUs after it lifts (a drag eases to its stop; quick taps run
    // together). Outside a gesture, changes are taken as they are and not reported: only the
    // user's touch makes the document ask the device for something (RC_ACTION_PATTERNS.md,
    // pattern 7), so a value that follows a page or a clock never fires.
    void checkWatched(bool gesture) {
        if (gesture) return;
        for (auto& w : watched) {
            std::string now = valueOf(*w.var);
            if (now == w.last) continue;
            w.last = now;
            if (!gestureEnded) continue;
            ESP_LOGI(TAG, "named %s = %s (document)", w.var->varName.c_str(), now.c_str());
            std::vector<NamedListener> fns;
            {
                std::lock_guard<std::mutex> g(sListenLock);
                for (auto& [pattern, fn] : sListeners)
                    if (matches(pattern, w.var->varName)) fns.push_back(fn);
            }
            for (auto fn : fns) fn(w.var->varName, now);
        }
        gestureEnded = false;
    }
    bool gestureEnded = false;   // a gesture ended since the last look: report what changed

    // The value a named variable has now, as text: a number, "#AARRGGBB", or the string.
    std::string valueOf(const rccore::NamedVariable& v) const {
        char b[32];
        switch (v.varType) {
            case 0: return ctx->getText(v.varId);
            case 1: snprintf(b, sizeof b, "%g", ctx->getFloat(v.varId)); return b;
            case 2: snprintf(b, sizeof b, "#%08X", (unsigned) ctx->getColor(v.varId)); return b;
            case 4: return std::to_string(ctx->getInteger(v.varId));
            default: return {};
        }
    }

    std::vector<const rccore::NamedVariable*> vars;   // the document's exported names

    const rccore::NamedVariable* named(const std::string& n) const {
        for (auto* v : vars)
            if (v->varName == n) return v;
        return nullptr;
    }

    bool setNamed(const std::string& n, const std::string& value) {
        auto* v = named(n);
        if (!v) return false;
        if (!setValue(*v, value)) return false;
        // A value set from outside is no change of the document's.
        for (auto& w : watched)
            if (w.var == v) w.last = valueOf(*v);
        return true;
    }

    // NamedVariable types, as in androidx: 0 string, 1 float, 2 color, 4 integer.
    bool setValue(const rccore::NamedVariable& nv, const std::string& value) {
        const auto* v = &nv;
        switch (v->varType) {
            case 0: ctx->overrideText(v->varId, value); return true;
            case 1: ctx->overrideFloat(v->varId, std::strtof(value.c_str(), nullptr)); return true;
            case 2: {
                const char* s = value.c_str() + (value[0] == '#');
                uint32_t c = (uint32_t) std::strtoul(s, nullptr, 16);
                if (std::strlen(s) <= 6) c |= 0xFF000000u;
                ctx->overrideColor(v->varId, (int) c);
                return true;
            }
            case 4: ctx->overrideInteger(v->varId, (int) std::strtol(value.c_str(), nullptr, 10)); return true;
            default: return false;
        }
    }

    bool ownsBanner = false;      // the document shows the banner text itself
    int64_t tDoc = 0, tRaster = 0;   // accumulated: engine scene build vs ThorVG raster
};

static Player sPlayer;
static uint32_t* sBufs[2];
static int sBack = 0;
static bool sDirty = true;
static bool sStatsNow = false;
// When the document wants its next frame (esp_timer us; INT64_MAX: never), and frames drawn.
static std::atomic<int64_t> sNextAtUs{0};
static std::atomic<bool> sContinuous{false};   // the last frame asked for the next at once
static std::atomic<uint32_t> sFrames{0};

static int64_t wallMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t) tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// The delay to the document's next frame, as the engine schedules it (getRepaintDelay, which
// mirrors androidx RemoteComposeState.getOpsToUpdate), in ms: -1 none, 0 at once.
// - It is given the wall clock, not uptime: its second and minute boundaries are taken modulo
//   the time it's given, so with uptime a minute hand moved up to 59 s late.
// - The engine, like Java, has no case for hours (ID_TIME_IN_HR): a document that reads
//   only the hour would never redraw. Added here: just past the next local hour.
static int nextDelayMs() {
    auto& ctx = *sPlayer.ctx;
    const int64_t now = wallMs();
    int delayMs = ctx.getRepaintDelay(now);
    if (sPlayer.pc->doesNeedsRepaint()) return 0;
    if (ctx.hasListeners(rccore::RemoteContext::ID_TIME_IN_HR)) {
        const time_t t = (time_t) (now / 1000);
        struct tm l;
        localtime_r(&t, &l);
        const int toHour = 2 + (3600 - (l.tm_min * 60 + l.tm_sec)) * 1000 - (int) (now % 1000);
        if (delayMs < 0 || toHour < delayMs) delayMs = toHour;
    }
    return delayMs;
}

// Cross-task inbox.
static std::mutex sLock;
static std::vector<uint8_t> sPendingDoc;
static std::string sPendingName;
static bool sPendingFit = false;
static bool sHavePending = false;
static std::vector<std::pair<std::string, std::string>> sPendingNamed;
static std::map<int, float> sPendingSystem;
static std::string sBannerTitle, sBannerDetail;
static uint32_t sBannerArgb = 0;
static bool sBannerChanged = false;

// What other tasks may read of the showing document (sLock): its size and bytes, and its
// named variables' values as of the last frame drawn.
static DocInfo sInfo;
static std::shared_ptr<const std::vector<uint8_t>> sInfoBytes;
static std::vector<std::string> sNames;   // the showing document's named variables

static const char* typeName(int t) {
    switch (t) {
        case 0: return "text";
        case 1: return "number";
        case 2: return "color";
        case 4: return "integer";
        default: return "other";
    }
}

// Player task, after a load.
static void publishInfo() {
    std::lock_guard<std::mutex> g(sLock);
    sInfo = {sPlayer.name, sPlayer.dpW, sPlayer.dpH, sPlayer.rw, sPlayer.rh, sPlayer.fit, sPlayer.scale,
             sPlayer.bytes ? sPlayer.bytes->size() : 0};
    sInfoBytes = sPlayer.bytes;
    sNames.clear();
    for (auto* v : sPlayer.vars) sNames.push_back(v->varName);
}

// Values are read on request, by the player task between frames: copying them after every
// frame would churn internal RAM (small allocations land there), which the Tab5's radio link
// needs at boot.
struct ValuesRequest {
    std::vector<NamedValue>* out;
    SemaphoreHandle_t done;
};
static ValuesRequest* sValuesRequest;   // sLock

static void serviceValues() {
    ValuesRequest* r;
    {
        std::lock_guard<std::mutex> g(sLock);
        r = sValuesRequest;
        sValuesRequest = nullptr;
    }
    if (!r) return;
    for (auto* v : sPlayer.vars) r->out->push_back({v->varName, typeName(v->varType), sPlayer.valueOf(*v)});
    xSemaphoreGive(r->done);
}

// ── Events: what happened on the device, for a host to poll (rc.events, rc.last_touch) ──

static std::mutex sEventLock;
static std::vector<Event> sEvents;   // the last kEvents, oldest first
static constexpr size_t kEvents = 32;
static uint32_t sEventSeq = 0;
static TouchInfo sTouch;

void pushEvent(Event e) {
    std::lock_guard<std::mutex> g(sEventLock);
    e.seq = ++sEventSeq;
    e.atUs = esp_timer_get_time();
    if (sEvents.size() >= kEvents) sEvents.erase(sEvents.begin());
    sEvents.push_back(std::move(e));
}

std::vector<Event> events(uint32_t since, uint32_t* latest) {
    std::lock_guard<std::mutex> g(sEventLock);
    std::vector<Event> out;
    for (auto& e : sEvents)
        if (e.seq > since) out.push_back(e);
    if (latest) *latest = sEventSeq;
    return out;
}

static std::atomic<bool> sSwallow{false};
void swallowTouch() { sSwallow = true; }

TouchInfo lastTouch() {
    std::lock_guard<std::mutex> g(sEventLock);
    return sTouch;
}

// ── Frame capture (rc.screenshot) ──

struct CaptureRequest {
    Frame* out;
    int w, h;
    bool gray;
    SemaphoreHandle_t done;
};
static CaptureRequest* sCapture;   // sLock

// Player task: the last frame presented, resampled to the request's size.
static void serviceCapture(const uint32_t* src) {
    CaptureRequest* r;
    {
        std::lock_guard<std::mutex> g(sLock);
        r = sCapture;
        sCapture = nullptr;
    }
    if (!r) return;
    Frame& f = *r->out;
    const int ch = r->gray ? 1 : 3;
    f.w = r->w; f.h = r->h; f.channels = ch;
    f.pixels.assign((size_t) f.w * f.h * ch, 0);
    const int levels = board::kGrays;
    for (int y = 0; y < f.h; y++) {
        const uint32_t* row = src + (size_t) std::min(sPlayer.rh - 1, y * sPlayer.rh / f.h) * sPlayer.rw;
        uint8_t* d = f.pixels.data() + (size_t) y * f.w * ch;
        for (int x = 0; x < f.w; x++) {
            const uint32_t p = row[std::min(sPlayer.rw - 1, x * sPlayer.rw / f.w)];
            const int r8 = (p >> 16) & 0xFF, g8 = (p >> 8) & 0xFF, b8 = p & 0xFF;
            if (ch == 3) { d[0] = r8; d[1] = g8; d[2] = b8; d += 3; continue; }
            int lum = (r8 * 77 + g8 * 151 + b8 * 28) >> 8;
            if (levels > 1) lum = (lum * (levels - 1) + 127) / 255 * 255 / (levels - 1);   // as the panel's greys
            *d++ = (uint8_t) lum;
        }
    }
    xSemaphoreGive(r->done);
}

bool capture(Frame& out, int w, int h, bool gray, int timeoutMs) {
    if (w <= 0 || h <= 0) {
        const DocInfo i = docInfo();
        w = i.renderW;
        h = i.renderH;
    }
    CaptureRequest r{&out, w, h, gray, xSemaphoreCreateBinary()};
    if (!r.done) return false;
    {
        std::lock_guard<std::mutex> g(sLock);
        if (sCapture) { vSemaphoreDelete(r.done); return false; }   // one at a time
        sCapture = &r;
    }
    bool ok = xSemaphoreTake(r.done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    if (!ok) {
        std::lock_guard<std::mutex> g(sLock);
        if (sCapture == &r) sCapture = nullptr;
        else ok = xSemaphoreTake(r.done, portMAX_DELAY) == pdTRUE;   // taken just now: let it finish
    }
    vSemaphoreDelete(r.done);
    return ok;
}

// ── Banner overlay ──────────────────────────────────────────────────────────────────────

static void addBanner(kobentovg::ThorvgPaintContext& pc, int rw, int rh) {
    std::string title, detail;
    uint32_t argb;
    {
        std::lock_guard<std::mutex> g(sLock);
        title = sBannerTitle;
        detail = sBannerDetail;
        argb = sBannerArgb;
    }
    if (title.empty() || sPlayer.ownsBanner) return;
    // Banner metrics are in Tab5 panel pixels (density 2): scale to this render size and density.
    const float s = (float) rw / board::kWidth * kDensity / 2;
    const float m = 24 * s, h = (detail.empty() ? 120 : 170) * s;
    const float x = m, y = rh - h - m, w = rw - 2 * m;

    auto card = tvg::Shape::gen();
    card->appendRect(x, y, w, h, 28 * s, 28 * s);
    card->fill(24, 26, 32, 236);
    pc.addOverlay(card);
    if (argb) {
        auto bar = tvg::Shape::gen();
        bar->appendRect(x, y, 14 * s, h, 7 * s, 7 * s);
        bar->fill((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, 255);
        pc.addOverlay(bar);
    }
    auto text = [&](const std::string& str, const char* font, float pt, float ty, uint8_t a) {
        auto t = tvg::Text::gen();
        if (t->font(font) != tvg::Result::Success) { tvg::Paint::rel(t); return; }
        t->size(pt * s);
        t->text(str.c_str());
        t->fill(255, 255, 255);
        t->opacity(a);
        t->translate(x + 44 * s, ty);
        pc.addOverlay(t);
    };
    text(title, "rc-sans-bold", 30, y + 30 * s, 255);
    if (!detail.empty()) text(detail, "rc-sans", 24, y + 92 * s, 200);
}

// ── Player-task API ─────────────────────────────────────────────────────────────────────

bool load(const uint8_t* data, size_t size, const char* name, bool fit) {
    bool ok = sPlayer.load(std::vector<uint8_t>(data, data + size), name, sBufs[sBack], fit);
    ESP_LOGI(TAG, "doc '%s' (%u bytes): %s", name, (unsigned) size, ok ? "ok" : "FAILED");
    if (ok) {
        publishInfo();
        pushEvent({0, 0, "document", 0, 0, name, {}});
    }
    sDirty = true;
    return ok;
}

void setRenderDiv(int div) {
    if (div < 1 || div > 2 || div == sRenderDiv) return;
    sRenderDiv = div;
    std::vector<uint8_t> bytes = *sPlayer.bytes;
    std::string name = sPlayer.name;
    sPlayer.load(std::move(bytes), name.c_str(), sBufs[sBack], sPlayer.fit);
    publishInfo();
    sDirty = true;
}

void requestStats() { sStatsNow = true; }

const std::vector<uint8_t>& currentBytes() { return *sPlayer.bytes; }

std::vector<std::string> namedVariables() {
    std::vector<std::string> out;
    for (auto* v : sPlayer.vars) out.push_back(v->varName + ":" + std::to_string(v->varType));
    return out;
}

void bench() {
    board::presentWait();
    uint32_t* px = sBufs[sBack];
    const int w = sPlayer.rw, h = sPlayer.rh;
    std::unique_ptr<tvg::SwCanvas> c(tvg::SwCanvas::gen());
    c->target(px, w, w, h, tvg::ColorSpace::ARGB8888);
    auto run = [&](const char* name, bool clear, int alpha, int nRects) {
        const int N = 5;
        int64_t t0 = esp_timer_get_time();
        for (int i = 0; i < N; i++) {
            c->remove();
            for (int k = 0; k < nRects; k++) {
                auto r = tvg::Shape::gen();
                r->appendRect(0, 0, (float) w, (float) h);
                r->fill(40, 80, 160, alpha);
                c->add(r);
            }
            c->draw(clear);
            c->sync();
        }
        printf("bench %-28s %6.1f ms\n", name, (esp_timer_get_time() - t0) / 1000.0f / N);
    };
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < 5; i++) memset(px, 0, (size_t) w * h * 4);
    printf("bench %-28s %6.1f ms\n", "memset", (esp_timer_get_time() - t0) / 1000.0f / 5);
    run("clear only", true, 255, 0);
    run("opaque rect, no clear", false, 255, 1);
    run("clear + opaque rect", true, 255, 1);
    run("blended rect (a=128)", false, 128, 1);
    run("4 blended rects", false, 128, 4);
    sDirty = true;
}

// ── Any-task API ────────────────────────────────────────────────────────────────────────

void show(std::vector<uint8_t> bytes, std::string name, bool fit) {
    std::lock_guard<std::mutex> g(sLock);
    sPendingDoc = std::move(bytes);
    sPendingName = std::move(name);
    sPendingFit = fit;
    sHavePending = true;
}

void setBanner(const std::string& title, const std::string& detail, uint32_t argb) {
    std::lock_guard<std::mutex> g(sLock);
    if (title == sBannerTitle && detail == sBannerDetail && argb == sBannerArgb) return;
    sBannerTitle = title;
    sBannerDetail = detail;
    sBannerArgb = argb;
    sBannerChanged = true;
    // A document that declares these draws the banner itself (see Player::ownsBanner).
    char color[12];
    snprintf(color, sizeof color, "#%08X", (unsigned) (argb ? argb : 0xFF9E9E9Eu));
    sPendingNamed.emplace_back("system.banner_title", title);
    sPendingNamed.emplace_back("system.banner_detail", detail);
    sPendingNamed.emplace_back("system.banner_color", color);
}

void setNamed(std::string name, std::string value) {
    std::lock_guard<std::mutex> g(sLock);
    sPendingNamed.emplace_back(std::move(name), std::move(value));
}

void listenNamed(std::string pattern, NamedListener fn) {
    std::lock_guard<std::mutex> g(sListenLock);
    sListeners.emplace_back(std::move(pattern), fn);
}

bool declaresNamed(const std::string& pattern) {
    std::lock_guard<std::mutex> g(sLock);
    for (auto& n : sNames)
        if (matches(pattern, n)) return true;
    return false;
}

void setSystem(int id, float value) {
    std::lock_guard<std::mutex> g(sLock);
    sPendingSystem[id] = value;
}

std::string currentName() {
    std::lock_guard<std::mutex> g(sLock);
    return sInfo.name;
}

DocInfo docInfo() {
    std::lock_guard<std::mutex> g(sLock);
    return sInfo;
}

std::shared_ptr<const std::vector<uint8_t>> docBytes() {
    std::lock_guard<std::mutex> g(sLock);
    return sInfoBytes;
}

int nextFrameMs() {
    if (sContinuous) return 0;
    const int64_t at = sNextAtUs;
    if (at == INT64_MAX) return -1;
    // Due or overdue (a variable just changed, say): 1, not 0, which means animating.
    return (int) std::max<int64_t>(1, (at - esp_timer_get_time()) / 1000);
}

uint32_t framesDrawn() { return sFrames; }

std::vector<std::string> namedVariableNames() {
    std::lock_guard<std::mutex> g(sLock);
    return sNames;
}

bool namedValues(std::vector<NamedValue>& out, int timeoutMs) {
    ValuesRequest r{&out, xSemaphoreCreateBinary()};
    if (!r.done) return false;
    {
        std::lock_guard<std::mutex> g(sLock);
        if (sValuesRequest) { vSemaphoreDelete(r.done); return false; }   // one at a time
        sValuesRequest = &r;
    }
    bool ok = xSemaphoreTake(r.done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    if (!ok) {
        std::lock_guard<std::mutex> g(sLock);
        if (sValuesRequest == &r) sValuesRequest = nullptr;
        else ok = xSemaphoreTake(r.done, portMAX_DELAY) == pdTRUE;   // taken just now: let it finish
    }
    vSemaphoreDelete(r.done);
    return ok;
}

// ── The task ────────────────────────────────────────────────────────────────────────────

static void drainInbox() {
    std::vector<uint8_t> doc;
    std::string name;
    bool haveDoc = false, banner = false, fit = false;
    std::vector<std::pair<std::string, std::string>> named;
    std::map<int, float> system;
    {
        std::lock_guard<std::mutex> g(sLock);
        if (sHavePending) {
            doc = std::move(sPendingDoc);
            name = std::move(sPendingName);
            fit = sPendingFit;
            sHavePending = false;
            haveDoc = true;
        }
        named.swap(sPendingNamed);
        system.swap(sPendingSystem);
        banner = sBannerChanged;
        sBannerChanged = false;
    }
    if (haveDoc) {
        size_t n = doc.size();
        bool ok = sPlayer.load(std::move(doc), name.c_str(), sBufs[sBack], fit);
        ESP_LOGI(TAG, "doc '%s' (%u bytes): %s", name.c_str(), (unsigned) n, ok ? "showing" : "FAILED, kept the old one");
        if (ok) {
            publishInfo();
            pushEvent({0, 0, "document", 0, 0, name, {}});
        }
        sDirty = true;
    }
    for (auto& [k, v] : named) {
        auto it = sNamedValues.find(k);
        if (it != sNamedValues.end() && it->second == v) continue;
        sNamedValues[k] = v;
        if (sPlayer.setNamed(k, v)) sDirty = true;
    }
    for (auto& [id, v] : system) {
        auto it = sSystemValues.find(id);
        if (it != sSystemValues.end() && it->second == v) continue;
        sSystemValues[id] = v;
        sPlayer.ctx->loadFloat(id, v);
        sDirty = true;
    }
    if (banner) sDirty = true;
}

// The motion sensors, in Android's system variables (androidx RemoteContext
// FLOAT_ACCELERATION_X..Z = 17..19, FLOAT_GYRO_ROT_X..Z = 20..22; rccore has no names for
// them). Read at 50 Hz, and only while the document uses one, so other documents don't
// redraw for every tremor of the board. Player task.
static void feedMotion(int64_t now) {
    static int64_t next = 0;
    static float last[6];
    if (now < next) return;
    next = now + 20000;
    auto& ctx = *sPlayer.ctx;
    bool wanted = false;
    for (int id = 17; id <= 22 && !wanted; id++) wanted = ctx.hasListeners(id);
    if (!wanted) return;
    const board::Motion m = board::readMotion();
    if (!m.valid) return;
    const float v[6] = {m.ax, m.ay, m.az, m.gx, m.gy, m.gz};
    for (int i = 0; i < 6; i++) {
        ctx.loadFloat(17 + i, v[i]);
        if (std::fabs(v[i] - last[i]) > 0.05f) sDirty = true;
        last[i] = v[i];
    }
}

static constexpr int64_t kSettleUs = 300000;

// system.touch_down: 1 while a finger is down, for a document that shows a drag's value only
// while it is dragged. Player task.
static void setTouchDown(bool down) {
    sNamedValues["system.touch_down"] = down ? "1" : "0";
    if (sPlayer.setNamed("system.touch_down", down ? "1" : "0")) sDirty = true;
}

static void task(void*) {
    int64_t gestureUntil = 0;   // a gesture is open until then (INT64_MAX while a finger is down)
    bool wasDown = false, moved = false;
    float downX = 0, downY = 0, lastX = 0, lastY = 0;
    int64_t downAt = 0;
    int frames = 0;
    int64_t tPresent = 0, statsAt = esp_timer_get_time();
    // Render only when input arrived, the document changed, or the engine's own schedule
    // (getRepaintDelay / needsRepaint) says a frame is due. A static document costs nothing.
    int64_t nextAt = 0;

    while (true) {
        // ── input ──
        board::Touch t = board::readTouch();
        // panel px -> screen dp -> the document's dp (a fitted document is scaled and offset)
        float x = (t.x / kDensity - sPlayer.ox) / sPlayer.scale;
        float y = (t.y / kDensity - sPlayer.oy) / sPlayer.scale;
        int64_t now = esp_timer_get_time();
        auto& doc = *sPlayer.doc;
        auto& ctx = *sPlayer.ctx;
        if (t.down) {
            std::lock_guard<std::mutex> g(sEventLock);
            if (!wasDown) sTouch.downs++;
            sTouch.down = true;
            sTouch.x = x; sTouch.y = y; sTouch.panelX = t.x; sTouch.panelY = t.y;
            sTouch.atUs = now;
        } else if (wasDown) {
            std::lock_guard<std::mutex> g(sEventLock);
            sTouch.down = false;
            sTouch.atUs = now;
        }
        if (sSwallow.exchange(false) && (t.down || wasDown)) downAt = INT64_MAX / 2;   // as after a long press
        if (t.down && !wasDown) {
            downX = lastX = x; downY = lastY = y; downAt = now; moved = false;
            if (gestureUntil == 0) sPlayer.checkWatched(false);   // take the values as they are now
            gestureUntil = INT64_MAX;
            setTouchDown(true);
            doc.touchDown(ctx, x, y);
        } else if (t.down) {
            if (std::fabs(x - downX) > 8 || std::fabs(y - downY) > 8) moved = true;
            if (x != lastX || y != lastY) doc.touchDrag(ctx, x, y);
            lastX = x; lastY = y;
            if (!moved && now - downAt > 1500000) {
                downAt = INT64_MAX / 2;                    // one long press per press
                pushEvent({0, 0, "long_press", x, y, {}, {}});
                if (sHooks.longPress) sHooks.longPress();
            }
        } else if (wasDown) {
            gestureUntil = now + kSettleUs;
            setTouchDown(false);
            doc.touchUp(ctx, lastX, lastY, 0, 0);
            // A long press is the player's (the launcher): its release is no click, or it
            // would land on whatever the long press just opened.
            bool tap = !moved && downAt != INT64_MAX / 2;
            if (tap) doc.onClick(ctx, lastX, lastY);
            if (tap) pushEvent({0, 0, "tap", lastX, lastY, {}, {}});
            if (tap && sHooks.tap) sHooks.tap(lastX, lastY);
        }
        if (t.down || wasDown) sDirty = true;
        wasDown = t.down;
        if (!sRaised.empty()) {
            auto raised = std::move(sRaised);
            sRaised.clear();
            for (auto& r : raised) {
                if (r.host && sHooks.hostAction) sHooks.hostAction(r.idOrType, r.a);
                else if (!r.host && sHooks.namedAction) sHooks.namedAction(r.a, r.idOrType, r.b);
            }
        }

        if (sHooks.tick) sHooks.tick();
        drainInbox();
        if (gestureUntil && now >= gestureUntil) {
            gestureUntil = 0;
            sPlayer.gestureEnded = true;
        }
        sPlayer.checkWatched(gestureUntil != 0);
        feedMotion(now);
        serviceCapture(sBufs[sBack ^ 1]);   // the frame last presented (one buffer on e-ink)
        serviceValues();

        // ── render ──
        if (!sDirty && now < nextAt) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        sPlayer.pc->retarget(sBufs[sBack]);
        const float k = kDensity / sRenderDiv;
        const float ks = k * sPlayer.scale;
        const tvg::Matrix base{ks, 0, k * sPlayer.ox, 0, ks, k * sPlayer.oy, 0, 0, 1};
        int64_t t0 = esp_timer_get_time();
        // A fitted document sits on black bars; anything else covers the screen.
        sPlayer.pc->beginFrame(base, sPlayer.fit ? 0xFF000000 : 0xFFFFFFFF);
        try {
            sPlayer.doc->paint(*sPlayer.ctx, kThemeLight);
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "paint threw: %s", e.what());
        }
        addBanner(*sPlayer.pc, sPlayer.rw, sPlayer.rh);
        int64_t t1 = esp_timer_get_time();
        sPlayer.pc->endFrame();
        int64_t t2 = esp_timer_get_time();
        sPlayer.tDoc += t1 - t0;
        sPlayer.tRaster += t2 - t1;
        board::presentAsync(sBufs[sBack], sPlayer.rw, sPlayer.rh);   // waits for the previous one
        tPresent += esp_timer_get_time() - t2;
        sBack ^= 1;
        frames++;
        sDirty = false;
        const int delayMs = nextDelayMs();
        nextAt = delayMs < 0 ? INT64_MAX : esp_timer_get_time() + (int64_t) delayMs * 1000;
        sNextAtUs = nextAt;
        sContinuous = delayMs == 0;
        sFrames++;

        if (sStatsNow || now - statsAt > 5000000) {
            ESP_LOGI(TAG, "%dx%d %.1f fps | doc %.1f + raster %.1f ms | present wait %.1f ms | %d tvg paints | heap int %u psram %u",
                     sPlayer.rw, sPlayer.rh, frames * 1e6f / (now - statsAt + 1),
                     sPlayer.tDoc / 1000.0f / frames, sPlayer.tRaster / 1000.0f / frames, tPresent / 1000.0f / frames,
                     sPlayer.pc->paintCount(), (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            frames = 0; tPresent = 0; statsAt = now; sStatsNow = false;
            sPlayer.tDoc = sPlayer.tRaster = 0;
        }
        vTaskDelay(1);   // let IDLE run (watchdog)
    }
}

bool start(const Hooks& hooks, const uint8_t* doc, size_t size, const char* name, bool fit) {
    sHooks = hooks;
    // Again after a document that didn't load: the fonts and frames are there already.
    static bool sInit = false;
    if (!sInit) {
        sInit = true;
        using Face = kobentovg::ThorvgPaintContext::Face;
        tvg::Initializer::init(0);
        kobentovg::ThorvgPaintContext::registerFont(Face::Sans, _binary_Roboto_Regular_ttf_start,
            _binary_Roboto_Regular_ttf_end - _binary_Roboto_Regular_ttf_start);
        kobentovg::ThorvgPaintContext::registerFont(Face::SansBold, _binary_Roboto_Bold_ttf_start,
            _binary_Roboto_Bold_ttf_end - _binary_Roboto_Bold_ttf_start);
        kobentovg::ThorvgPaintContext::registerFont(Face::Mono, _binary_RobotoMono_Regular_ttf_start,
            _binary_RobotoMono_Regular_ttf_end - _binary_RobotoMono_Regular_ttf_start);

        // Two render targets: the CPU draws into one while the PPA converts the other. E-ink
        // needs one: its presentAsync converts the frame before it returns (2 MB on the PaperS3).
        for (int i = 0; i < (board::kEink ? 1 : 2); i++) {
            sBufs[i] = (uint32_t*) heap_caps_aligned_alloc(128, (size_t) board::kWidth * board::kHeight * 4,
                                                           MALLOC_CAP_SPIRAM);
            if (!sBufs[i]) { ESP_LOGE(TAG, "no frame memory"); return false; }
        }
        if (board::kEink) sBufs[1] = sBufs[0];
    }
    if (!sBufs[0] || !load(doc, size, name, fit)) return false;
    // Rendering is deep C++ (layout recursion, ThorVG); give it a large stack.
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    // Boards short of internal RAM (the classic ESP32 needs it for Wi-Fi and DMA) keep the
    // engine's deep stack in PSRAM.
    return xTaskCreatePinnedToCoreWithCaps(task, "rcstage", 64 * 1024, nullptr, 5, nullptr, 1,
                                           MALLOC_CAP_SPIRAM) == pdPASS;
#else
    return xTaskCreatePinnedToCore(task, "rcstage", 64 * 1024, nullptr, 5, nullptr, 1) == pdPASS;
#endif
}

}  // namespace rcstage
