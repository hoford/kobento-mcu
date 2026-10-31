// RemoteCompose player for the STM32U5A9J-DK: rccore (the engine) -> ThorvgPaintContext
// (ARGB8888, in RAM) -> DMA2D copy -> the round panel. One loop, no RTOS: touch, the USER
// button, the log port's console, then a frame only when something changed or the
// document's schedule says one is due. The same engine and ThorVG backend the ESP32 players
// run (../esp32-rc/components), with this board's layer underneath.
//
// Console (115200 on the ST-LINK's virtual COM port), one command per line:
//   list | doc N | next | prev | stats | time UNIX_SECONDS
//   hold | run      stop and restart drawing (a screenshot reads a finished frame)
// A long press (1.5 s) or the USER button shows the next document.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <sys/time.h>
#include <vector>

#include "board.h"
#include "embedded.h"
#include "rccore/CoreDocument.h"
#include "rccore/RemoteContext.h"
#include "rccore/WireBuffer.h"
#include "rccore/operations/DrawOperations.h"
#include "kobentovg/ThorvgPaintContext.h"

static constexpr float kDensity = BOARD_DENSITY;
static constexpr int kThemeLight = -3;
static constexpr int kW = BOARD_WIDTH, kH = BOARD_HEIGHT;

// The frame ThorVG draws into; the panel's own buffer is the BSP's (behind the GFXMMU).
alignas(16) static uint32_t sFrame[kW * kH];

static int64_t wallMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t) tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

struct Player {
    int index = -1;
    std::unique_ptr<rccore::CoreDocument> doc;
    std::unique_ptr<rccore::RemoteContext> ctx;
    std::unique_ptr<kobentovg::ThorvgPaintContext> pc;
    int dpW = 0, dpH = 0;
    bool fit = false;
    float scale = 1, ox = 0, oy = 0;
    uint64_t tDoc = 0, tRaster = 0, tPresent = 0;

    const rccore::NamedVariable* named(const std::string& n) const {
        for (auto& op : doc->getOperations())
            if (auto* v = dynamic_cast<const rccore::NamedVariable*>(op.get()); v && v->varName == n) return v;
        return nullptr;
    }

    // As rcstage: NamedVariable types 0 string, 1 float, 2 color, 4 integer.
    bool setNamed(const std::string& n, const std::string& value) {
        auto* v = named(n);
        if (!v) return false;
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

    // The documents live in flash for good, so the engine reads them in place. The old
    // document goes first: RAM is short, and two parsed documents may not fit.
    bool load(int i) {
        const EmbeddedDoc& e = kEmbeddedDocs[i];
        pc.reset();
        ctx.reset();
        doc.reset();
        uint32_t before = board_heap_free();
        uint64_t t0 = board_us();
        auto next = std::make_unique<rccore::CoreDocument>();
        try {
            rccore::WireBuffer buf(e.start, (size_t) (e.end - e.start));
            if (!next->initFromBuffer(buf)) {
                printf("doc %s: parse failed\r\n", e.name);
                return false;
            }
        } catch (const std::exception& ex) {
            printf("doc %s: parse threw: %s\r\n", e.name, ex.what());
            return false;
        }
        doc = std::move(next);
        index = i;
        const int winW = (int) (kW / kDensity), winH = (int) (kH / kDensity);
        constexpr int kFitDefault = 600;   // as rcstage: a size-less document was drawn for 600 x 600
        fit = e.fit;
        dpW = !fit ? winW : doc->getWidth() > 0 ? doc->getWidth() : kFitDefault;
        dpH = !fit ? winH : doc->getHeight() > 0 ? doc->getHeight() : kFitDefault;
        scale = fit ? std::min((float) winW / dpW, (float) winH / dpH) : 1.0f;
        ox = fit ? (winW - dpW * scale) / 2 : 0;
        oy = fit ? (winH - dpH * scale) / 2 : 0;
        doc->setWidth(dpW);
        doc->setHeight(dpH);
        ctx = std::make_unique<rccore::RemoteContext>();
        pc = std::make_unique<kobentovg::ThorvgPaintContext>(*ctx, sFrame, kW, kH);
        ctx->setPaintContext(pc.get());
        ctx->setDocument(doc.get());
        ctx->mWidth = (float) dpW;
        ctx->mHeight = (float) dpH;
        doc->registerListeners(*ctx);
        ctx->loadFloat(rccore::RemoteContext::ID_WINDOW_WIDTH, (float) dpW);
        ctx->loadFloat(rccore::RemoteContext::ID_WINDOW_HEIGHT, (float) dpH);
        ctx->hostAction = [](int id, const std::string& md) { printf("hostAction(%d, \"%s\")\r\n", id, md.c_str()); };
        ctx->hostNamedAction = [](const std::string& n, int, const std::string& v) {
            printf("namedAction %s = %s\r\n", n.c_str(), v.c_str());
        };
        doc->applyDataOperations(*ctx, kThemeLight);
        setNamed("system.board", BOARD_DISPLAY_NAME);
        setNamed("system.screen_round", BOARD_ROUND ? "1" : "0");
        setNamed("system.screen_eink", "0");
        printf("doc %d %s: %u bytes, %ux%u dp%s, parsed in %lu ms, %lu KB RAM (%lu KB free)\r\n", i, e.name,
               (unsigned) (e.end - e.start), (unsigned) dpW, (unsigned) dpH, fit ? " fitted" : "",
               (unsigned long) ((board_us() - t0) / 1000), (unsigned long) ((before - board_heap_free()) / 1024),
               (unsigned long) (board_heap_free() / 1024));
        return true;
    }

    // As rcstage's nextDelayMs: -1 never, 0 at once.
    int nextDelayMs() {
        const int64_t now = wallMs();
        int delayMs = ctx->getRepaintDelay(now);
        if (pc->doesNeedsRepaint()) return 0;
        if (ctx->hasListeners(rccore::RemoteContext::ID_TIME_IN_HR)) {
            const time_t t = (time_t) (now / 1000);
            struct tm l;
            localtime_r(&t, &l);
            const int toHour = 2 + (3600 - (l.tm_min * 60 + l.tm_sec)) * 1000 - (int) (now % 1000);
            if (delayMs < 0 || toHour < delayMs) delayMs = toHour;
        }
        return delayMs;
    }

    void frame() {
        const float ks = kDensity * scale;
        const tvg::Matrix base{ks, 0, kDensity * ox, 0, ks, kDensity * oy, 0, 0, 1};
        uint64_t t0 = board_us();
        // A fitted document sits on black; anything else covers the screen.
        pc->beginFrame(base, fit ? 0xFF000000 : 0xFFFFFFFF);
        try {
            doc->paint(*ctx, kThemeLight);
        } catch (const std::exception& e) {
            printf("paint threw: %s\r\n", e.what());
        }
        uint64_t t1 = board_us();
        pc->endFrame();
        uint64_t t2 = board_us();
        board_present(sFrame);
        tDoc += t1 - t0;
        tRaster += t2 - t1;
        tPresent += board_us() - t2;
    }
};

static Player sPlayer;
static bool sHold = false;   // "hold": no frames, so tools/snap.py reads a whole one

static void show(int i) {
    const int n = (int) kEmbeddedDocCount;
    i = ((i % n) + n) % n;
    if (!sPlayer.load(i)) {
        // Fall back to the next one that loads.
        for (int k = 1; k < n && !sPlayer.load((i + k) % n); k++) {}
    }
}

static void console(const char* line, bool& dirty) {
    char cmd[16] = {0};
    long long arg = 0;
    int have = sscanf(line, "%15s %lld", cmd, &arg);
    if (have < 1) return;
    if (!strcmp(cmd, "list")) {
        for (size_t i = 0; i < kEmbeddedDocCount; i++)
            printf("%c %2u %s\r\n", (int) i == sPlayer.index ? '*' : ' ', (unsigned) i, kEmbeddedDocs[i].name);
    } else if (!strcmp(cmd, "doc") && have == 2) {
        show((int) arg);
        dirty = true;
    } else if (!strcmp(cmd, "next") || !strcmp(cmd, "prev")) {
        show(sPlayer.index + (cmd[0] == 'n' ? 1 : -1));
        dirty = true;
    } else if (!strcmp(cmd, "time") && have == 2) {
        board_set_time(arg);
        dirty = true;
        printf("time set\r\n");
    } else if (!strcmp(cmd, "hold") || !strcmp(cmd, "run")) {
        sHold = cmd[0] == 'h';
        printf("%s\r\n", sHold ? "held" : "running");
    } else if (!strcmp(cmd, "stats")) {
        printf("heap free %lu KB\r\n", (unsigned long) (board_heap_free() / 1024));
    } else {
        printf("commands: list | doc N | next | prev | stats | time UNIX_SECONDS | hold | run\r\n");
    }
}

int main() {
    if (!board_init()) {
        printf("board init failed\r\n");
        while (true) {}
    }
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);   // US Pacific, as the ESP32 players
    tzset();
    tvg::Initializer::init(0);
    using Face = kobentovg::ThorvgPaintContext::Face;
    kobentovg::ThorvgPaintContext::registerFont(Face::Sans, kFontSans, kFontSansSize);
    kobentovg::ThorvgPaintContext::registerFont(Face::SansBold, kFontSansBold, kFontSansBoldSize);
    kobentovg::ThorvgPaintContext::registerFont(Face::Mono, kFontMono, kFontMonoSize);
    printf("rcplayer: %u documents, heap free %lu KB\r\n", (unsigned) kEmbeddedDocCount,
           (unsigned long) (board_heap_free() / 1024));
    show(0);

    bool dirty = true, wasDown = false, moved = false, longDone = false;
    float downX = 0, downY = 0, lastX = 0, lastY = 0;
    uint32_t downAt = 0, statsAt = board_ms();
    uint64_t nextAt = 0;
    int frames = 0;
    char line[64];
    size_t lineLen = 0;

    while (true) {
        // ── console ──
        for (int c; (c = board_getchar()) >= 0;) {
            if (c == '\r' || c == '\n') {
                line[lineLen] = 0;
                if (lineLen) console(line, dirty);
                lineLen = 0;
            } else if (lineLen + 1 < sizeof line) {
                line[lineLen++] = (char) c;
            }
        }
        // ── button ──
        if (board_button_pressed()) {
            show(sPlayer.index + 1);
            dirty = true;
        }
        // ── touch: panel px -> screen dp -> the document's dp ──
        int px = 0, py = 0;
        bool down = board_touch(&px, &py);
        float x = (px / kDensity - sPlayer.ox) / sPlayer.scale;
        float y = (py / kDensity - sPlayer.oy) / sPlayer.scale;
        auto& doc = *sPlayer.doc;
        auto& ctx = *sPlayer.ctx;
        uint32_t now = board_ms();
        if (down && !wasDown) {
            downX = lastX = x; downY = lastY = y; downAt = now; moved = false; longDone = false;
            doc.touchDown(ctx, x, y);
        } else if (down) {
            if (std::fabs(x - downX) > 8 || std::fabs(y - downY) > 8) moved = true;
            if (x != lastX || y != lastY) doc.touchDrag(ctx, x, y);
            lastX = x; lastY = y;
            if (!moved && !longDone && now - downAt > 1500) {
                longDone = true;   // the next document; its release is no click
                show(sPlayer.index + 1);
            }
        } else if (wasDown && !longDone) {
            doc.touchUp(ctx, lastX, lastY, 0, 0);
            if (!moved) doc.onClick(ctx, lastX, lastY);
        }
        if (down || wasDown) dirty = true;
        wasDown = down;

        // ── frame ──
        uint64_t us = board_us();
        if (!sHold && (dirty || us >= nextAt)) {
            sPlayer.frame();
            frames++;
            dirty = false;
            int delayMs = sPlayer.nextDelayMs();
            nextAt = delayMs < 0 ? UINT64_MAX : board_us() + (uint64_t) delayMs * 1000;
        }
        if (now - statsAt >= 5000) {
            if (frames)
                printf("%.1f fps | doc %.1f + raster %.1f + present %.1f ms | %d paints | heap free %lu KB\r\n",
                       frames * 1000.0f / (now - statsAt), sPlayer.tDoc / 1000.0f / frames,
                       sPlayer.tRaster / 1000.0f / frames, sPlayer.tPresent / 1000.0f / frames,
                       sPlayer.pc->paintCount(), (unsigned long) (board_heap_free() / 1024));
            frames = 0;
            sPlayer.tDoc = sPlayer.tRaster = sPlayer.tPresent = 0;
            statsAt = now;
        }
    }
}
