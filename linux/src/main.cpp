// Kobento RC player for Linux — a display-only RemoteCompose player for the Raspberry Pi and
// the desktop, built on the same rccore + kobentovg engine as the ESP32/STM32 players.
//
// Structured like the reference rcviewer: a resizable desktop window (SDL2, so it sits on the
// PIXEL/Wayland or X11 desktop without taking over the console), the ThorVG software backend,
// a player loop, keyboard navigation, and a stdin console. No framebuffer; the desktop is left
// untouched.
//
//   rcplayer [DOCS ...] [options]
//     DOCS            .rc files and/or directories. A directory is scanned for *.rc (laid out
//                     at window size) and fit/*.rc and 3d/*.rc (laid out at their own size and
//                     scaled to fit). With none, "documents" in the current directory is used.
//   --size W H        initial window size (default 600 600)
//   --fullscreen      start borderless-fullscreen on the current desktop (F / F11 toggles)
//   --pi              Raspberry Pi preset: fullscreen + render-scale 2 + GPU upscale (nearest).
//                     Rasterizes at half resolution and lets the GPU scale up — the big win on
//                     the Pi Zero, where ThorVG's CPU raster is the bottleneck.
//   --render-scale N  rasterize at 1/N of the window and GPU-upscale (N 1..8, default 1; 2 on --pi)
//   --headless        no window; drive with `snap` (CI / no desktop session)
//   --density D       dp -> px for window-size layout (default 1.0)
//   --theme light|dark   default light
//   --round / --eink  prefer <name>_round / <name>_light variants; set the matching system var
//   --board NAME      system.board display name for documents (default "Raspberry Pi")
//   --named NAME=VAL  set a sticky named variable (repeatable)
//   --start NAME      first document to show (default: kobento_splash, else the first)
//   --auto SEC        auto-advance to the next document every SEC seconds (0 = off, default off)
//   --fps N           cap continuously-animating documents to N fps (default 30)
//   --once            render the first document once, write rcplayer-once.png, and exit
//
// Window keys (like rcviewer):  Left/Right or P/N  prev/next · Space pause · R reload · S snap
//                               F/F11 fullscreen · Q/Esc quit · click/drag/touch -> the document
// Console (stdin): list · next · prev · doc N · stats · bench [N] · act NAME[=VALUE] ·
//                  named [PREFIX] · snap [FILE] · res DIV · help · quit
#include "display.hpp"
#include "player.hpp"

#include "kobentovg/ThorvgPaintContext.h"

#include <thorvg.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>   // isatty

namespace fs = std::filesystem;
using kobento::Player;
using kobento::Input;
using kobento::Event;
using kobento::Phase;

// ── Documents ──────────────────────────────────────────────────────────────────────────────

struct Doc { std::string name; std::string path; bool fit; };
static std::vector<Doc> gDocs;

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static std::string stem(const fs::path& p) { return p.stem().string(); }

static void addFile(const fs::path& p, bool fit) {
    if (p.extension() == ".rc") gDocs.push_back({stem(p), p.string(), fit});
}

// Scan a directory the way the ESP32 build embeds it: *.rc at the top lay out at window size,
// fit/ and 3d/ at their own size scaled to fit. Recurses, so a tree of examples is picked up.
static void addDir(const fs::path& dir) {
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".rc") continue;
        // "fit" if any path component is "fit" or "3d" (their docs are designed at a fixed size).
        bool fit = false;
        for (const auto& part : e.path().lexically_relative(dir))
            if (part == "fit" || part == "3d") { fit = true; break; }
        gDocs.push_back({stem(e.path()), e.path().string(), fit});
    }
    std::sort(gDocs.begin(), gDocs.end(),
              [](const Doc& a, const Doc& b) { return a.name < b.name; });
}

// ── Variant selection (ported from esp32/firmware/rcplayer) ─────────────────────────────────

struct Variant { const char* suffix; bool mine; };
static Variant gVariants[2] = {{"_round", false}, {"_light", false}};

static int findExact(const std::string& name) {
    for (int i = 0; i < (int) gDocs.size(); i++)
        if (name == gDocs[i].name) return i;
    return -1;
}
static int findDoc(const std::string& name) {
    for (const Variant& v : gVariants) {
        if (!v.mine) continue;
        int i = findExact(name + v.suffix);
        if (i >= 0) return i;
    }
    return findExact(name);
}
static bool otherScreen(int i) {
    std::string n = gDocs[i].name;
    for (const Variant& v : gVariants) {
        const size_t len = strlen(v.suffix);
        if (n.size() > len && n.compare(n.size() - len, len, v.suffix) == 0) { n.resize(n.size() - len); break; }
    }
    return findDoc(n) != i;
}

// ── Console: a reader thread turns stdin lines into requests ────────────────────────────────

static std::queue<std::string> gQueue;
static std::mutex gMx;
static std::condition_variable gCv;

static void pushRequest(std::string line) {
    { std::lock_guard<std::mutex> g(gMx); gQueue.push(std::move(line)); }
    gCv.notify_one();
}

static void consoleThread() {
    std::string line;
    while (std::getline(std::cin, line)) pushRequest(line);
    // EOF on stdin. Only treat that as "quit" for an interactive terminal (where Ctrl-D means
    // quit). When stdin isn't a TTY — launched over SSH, backgrounded, or from /dev/null (the
    // kiosk case) — stdin is EOF from the start; the console is simply unavailable and the window
    // keys, touch, and --auto drive the player, so don't tear it down.
    if (isatty(STDIN_FILENO)) pushRequest("quit");
}

// ── Player state ────────────────────────────────────────────────────────────────────────────

static Player* gPlayer = nullptr;
static kobento::Display* gDisp = nullptr;
static int gCurrent = 0, gPrevious = 0;
static int gBacklight = 100;
static bool gDirty = true;        // a frame is owed (nav/reload/resize)
static int gRenderScale = 1;      // rasterize at 1/gRenderScale of the window, GPU-upscale
static int64_t gLastSwitchMs = 0; // when the current document was opened (for --auto)

static int64_t nowMs();   // defined below

static void updateTitle() {
    if (gDisp) gDisp->setTitle("Kobento · " + std::to_string(gCurrent) + " " + gPlayer->name());
}

static void open(int i, int step = 0) {
    const int n = (int) gDocs.size();
    const int from = gCurrent;
    for (int tries = 0; tries < n; tries++, i += step ? step : 1) {
        const int at = ((i % n) + n) % n;
        if (step && otherScreen(at)) continue;
        const Doc& d = gDocs[at];
        if (gPlayer->load(readFile(d.path), d.name, d.fit)) {
            gCurrent = at;
            if (at != from) gPrevious = from;
            gDirty = true;
            gLastSwitchMs = nowMs();
            updateTitle();
            return;
        }
        fprintf(stderr, "skip %s (load failed)\n", d.name.c_str());
    }
}

static void runHostAction(const std::string& name, const std::string& value) {
    if (name == "system.backlight") {
        int pct = value == "cycle" ? (gBacklight > 60 ? 60 : gBacklight > 30 ? 30 : 100)
                                   : (int) (strtof(value.c_str(), nullptr) * 100);
        gBacklight = std::max(5, std::min(100, pct));
        gPlayer->setNamed("system.backlight", std::to_string(gBacklight / 100.0f));
        gDirty = true;
    } else if (name == "doc.show") {
        int i = findDoc(value);
        if (i >= 0) open(i); else fprintf(stderr, "no document '%s'\n", value.c_str());
    } else if (name == "doc.next") {
        open(gCurrent + 1, 1);
    } else if (name == "doc.back") {
        open(gPrevious);
    } else {
        fprintf(stderr, "unknown host action '%s'\n", name.c_str());
    }
}

static void printHelp() {
    printf("window keys: Left/Right or P/N prev/next, Space pause, R reload, S snap,\n"
           "             F/F11 fullscreen, Q quit; click/drag/touch drives the document\n"
           "console: list | next | prev | doc N | stats | bench [N] | act NAME[=VALUE] |\n"
           "         named [PREFIX] | snap [FILE] | res DIV | help | quit\n");
}

static void snap(const std::string& arg) {
    std::string path = arg.empty() ? ("rcplayer-" + gPlayer->name() + ".png") : arg;
    printf(gPlayer->snap(path) ? "wrote %s\n" : "snap failed: %s\n", path.c_str());
}

// One console line. Returns false to quit.
static bool handle(const std::string& line) {
    std::string cmd, arg;
    size_t sp = line.find(' ');
    cmd = line.substr(0, sp);
    if (sp != std::string::npos) arg = line.substr(line.find_first_not_of(' ', sp));
    if (cmd.empty()) return true;

    if (cmd == "quit" || cmd == "q" || cmd == "exit") return false;
    else if (cmd == "help" || cmd == "?") printHelp();
    else if (cmd == "list") {
        for (int i = 0; i < (int) gDocs.size(); i++)
            printf("%c %3d %s%s\n", i == gCurrent ? '*' : ' ', i, gDocs[i].name.c_str(),
                   gDocs[i].fit ? "  (fit)" : "");
    } else if (cmd == "next") open(gCurrent + 1, 1);
    else if (cmd == "prev") open(gCurrent - 1, -1);
    else if (cmd == "doc") open(atoi(arg.c_str()));
    else if (cmd == "stats") gPlayer->printStats();
    else if (cmd == "bench") gPlayer->bench(arg.empty() ? 30 : atoi(arg.c_str()));
    else if (cmd == "res") { gPlayer->setRenderDiv(atoi(arg.c_str()), gDisp->width(), gDisp->height());
                             gDirty = true; printf("render %dx%d\n", gPlayer->renderW(), gPlayer->renderH()); }
    else if (cmd == "act") {
        size_t eq = arg.find('=');
        printf("act %s\n", arg.c_str());
        runHostAction(arg.substr(0, eq), eq == std::string::npos ? "" : arg.substr(eq + 1));
    } else if (cmd == "named") {
        for (auto& nv : gPlayer->namedValues())
            if (arg.empty() || nv.name.compare(0, arg.size(), arg) == 0)
                printf("  %-28s %-8s %s\n", nv.name.c_str(), nv.type.c_str(), nv.value.c_str());
    } else if (cmd == "snap") snap(arg);
    else printf("? %s (try 'help')\n", cmd.c_str());
    return true;
}

// ── main ────────────────────────────────────────────────────────────────────────────────────

static void loadFont(kobentovg::ThorvgPaintContext::Face face, const char* file) {
    static std::vector<std::vector<uint8_t>> keep;
    keep.push_back(readFile(std::string(FONT_DIR) + "/" + file));
    if (keep.back().empty() ||
        !kobentovg::ThorvgPaintContext::registerFont(face, keep.back().data(), keep.back().size()))
        fprintf(stderr, "warning: font %s not loaded\n", file);
}

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    std::string board = "Raspberry Pi", startName = "kobento_splash", title = "Kobento RC player";
    bool headless = false, round = false, eink = false, once = false, fullscreen = false, pi = false;
    int sizeW = 600, sizeH = 600, theme = -3, fps = 30, renderScale = 1;
    float density = 1.0f, autoSec = 0;
    std::vector<std::pair<std::string, std::string>> named;
    std::vector<std::string> paths;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](const char* d) { return i + 1 < argc ? argv[++i] : d; };
        if (a == "--size") { sizeW = atoi(next("600")); sizeH = atoi(next("600")); }
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--pi") pi = true;
        else if (a == "--render-scale") renderScale = std::max(1, std::min(8, atoi(next("1"))));
        else if (a == "--headless") headless = true;
        else if (a == "--density") density = (float) atof(next("1"));
        else if (a == "--theme") theme = strcmp(next("light"), "dark") ? -3 : -2;
        else if (a == "--round") round = true;
        else if (a == "--eink") eink = true;
        else if (a == "--board") board = next("Raspberry Pi");
        else if (a == "--start") startName = next("");
        else if (a == "--auto") autoSec = (float) atof(next("0"));
        else if (a == "--fps") fps = std::max(1, atoi(next("30")));
        else if (a == "--once") once = true;
        else if (a == "--title") title = next("Kobento RC player");
        else if (a == "--named") {
            std::string kv = next("");
            size_t eq = kv.find('=');
            if (eq != std::string::npos) named.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else if (a == "--help" || a == "-h") {
            printf("usage: rcplayer [DOCS ...] [options]  (see the header of main.cpp)\n");
            printHelp();
            return 0;
        } else if (a.size() && a[0] == '-') { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
        else paths.push_back(a);
    }

    if (paths.empty() && fs::is_directory("documents")) paths.push_back("documents");
    for (auto& p : paths) {
        if (fs::is_directory(p)) addDir(p);
        else addFile(p, p.find("/fit/") != std::string::npos);
    }
    if (gDocs.empty()) {
        fprintf(stderr, "no .rc documents found (give a directory or files; default is ./documents)\n");
        return 1;
    }

    // --pi preset: borderless fullscreen, half-resolution raster, nearest-neighbour GPU upscale.
    if (pi) {
        fullscreen = true;
        if (renderScale == 1) renderScale = 2;
    }
    gRenderScale = renderScale;

    tvg::Initializer::init(0);
    using Face = kobentovg::ThorvgPaintContext::Face;
    loadFont(Face::Sans, "Roboto-Regular.ttf");
    loadFont(Face::SansBold, "Roboto-Bold.ttf");
    loadFont(Face::Mono, "RobotoMono-Regular.ttf");

    std::unique_ptr<kobento::Display> disp;
    if (!headless) {
        kobento::WindowOptions opt;
        opt.w = sizeW; opt.h = sizeH; opt.title = title;
        opt.fullscreen = fullscreen;
        opt.linearFilter = !pi;              // nearest upscale under --pi for speed
        disp = kobento::openWindow(opt);
    }
    if (!disp) {
        if (!headless) fprintf(stderr, "no window (no desktop session?); running headless %dx%d — use `snap`\n",
                               sizeW, sizeH);
        disp = kobento::openHeadless(sizeW, sizeH);
    }
    gDisp = disp.get();
    // The window may differ from the requested size (fullscreen = desktop size). Rasterize at
    // 1/renderScale of the drawable and let present() upscale.
    const int winW = disp->width(), winH = disp->height();
    const int rw = std::max(1, winW / gRenderScale), rh = std::max(1, winH / gRenderScale);
    printf("Kobento RC player · %s · window %dx%d · render %dx%d%s · %d documents\n",
           disp->name(), winW, winH, rw, rh,
           gRenderScale > 1 ? (" (1/" + std::to_string(gRenderScale) + ")").c_str() : "",
           (int) gDocs.size());

    gVariants[0].mine = round;
    gVariants[1].mine = eink;

    Player player(rw, rh, density, theme);
    gPlayer = &player;
    player.setNamed("system.board", board);
    if (eink) player.setNamed("system.screen_eink", "1");
    if (round) player.setNamed("system.screen_round", "1");
    for (auto& [n, v] : named) player.setNamed(n, v);
    player.onHostAction = [](const std::string& n, const std::string& v) {
        pushRequest(v.empty() ? "act " + n : "act " + n + "=" + v);
    };

    gCurrent = gPrevious = std::max(0, findDoc(startName));
    const Doc& d0 = gDocs[gCurrent];
    if (!player.load(readFile(d0.path), d0.name, d0.fit)) open(gCurrent);
    updateTitle();

    if (once) {
        player.renderFrame();
        const char* out = "rcplayer-once.png";
        printf(player.snap(out) ? "wrote %s\n" : "snap failed: %s\n", out);
        tvg::Initializer::term();
        return 0;
    }

    std::thread reader(consoleThread);
    reader.detach();
    printHelp();

    bool running = true, paused = false;
    int64_t nextFrameAt = 0;                 // when the next frame is due (ms); 0 = now
    const int minFrameMs = 1000 / fps;       // fps cap for continuous documents
    const int64_t autoMs = (int64_t) (autoSec * 1000);   // 0 = no auto-advance
    std::vector<Event> events;

    // Pointer fling tracking: a ~20ms-trailing sample gives the release velocity (drawable px/s).
    float ptPrevX = 0, ptPrevY = 0, ptCurX = 0, ptCurY = 0;
    int64_t ptPrevT = 0, ptCurT = 0;
    // Window px -> render-buffer px (they differ when rasterizing below window size, --render-scale).
    auto bufX = [&](float x) { return x * (float) player.renderW() / std::max(1, disp->width()); };
    auto bufY = [&](float y) { return y * (float) player.renderH() / std::max(1, disp->height()); };

    while (running) {
        // 1. stdin console.
        for (;;) {
            std::string line;
            { std::lock_guard<std::mutex> g(gMx);
              if (gQueue.empty()) break;
              line = std::move(gQueue.front()); gQueue.pop(); }
            if (!handle(line)) { running = false; break; }
        }
        if (!running) break;

        // 2. window input.
        disp->poll(events);
        for (auto& e : events) {
            if (e.kind == Event::Close) running = false;
            else if (e.kind == Event::Resize) {
                // Rasterize at 1/gRenderScale of the new drawable; present() upscales to fill it.
                const int tw = std::max(1, e.w / gRenderScale), th = std::max(1, e.h / gRenderScale);
                if (e.w > 0 && e.h > 0 && (tw != player.renderW() || th != player.renderH())) {
                    player.setRenderDiv(gRenderScale, e.w, e.h); gDirty = true;
                }
            } else if (e.kind == Event::Key) {
                switch (e.key) {
                    case Input::Next: open(gCurrent + 1, 1); break;
                    case Input::Prev: open(gCurrent - 1, -1); break;
                    case Input::Reload: open(gCurrent); break;
                    case Input::PauseToggle: paused = !paused; updateTitle(); break;
                    case Input::Snap: snap(""); break;
                    case Input::Fullscreen: disp->toggleFullscreen(); gDirty = true; break;
                    case Input::Quit: running = false; break;
                }
            } else if (e.kind == Event::Pointer) {
                const int64_t t = nowMs();
                if (e.phase == Phase::Down) {
                    ptPrevX = ptCurX = e.px; ptPrevY = ptCurY = e.py; ptPrevT = ptCurT = t;
                    player.pointerDown(bufX(e.px), bufY(e.py));
                } else if (e.phase == Phase::Move) {
                    if (t - ptPrevT >= 20) { ptPrevX = ptCurX; ptPrevY = ptCurY; ptPrevT = ptCurT; }
                    ptCurX = e.px; ptCurY = e.py; ptCurT = t;
                    player.pointerMove(bufX(e.px), bufY(e.py));
                } else {  // Up
                    const float dt = (t - ptPrevT) / 1000.0f;
                    const float vx = dt > 0 ? (e.px - ptPrevX) / dt : 0;
                    const float vy = dt > 0 ? (e.py - ptPrevY) / dt : 0;
                    player.pointerUp(bufX(e.px), bufY(e.py), vx, vy);
                }
                gDirty = true;
            }
        }
        events.clear();
        if (!running) break;

        // 3. auto-advance after the dwell time (unaffected while paused).
        if (autoMs > 0 && !paused && nowMs() - gLastSwitchMs >= autoMs) open(gCurrent + 1, 1);

        // 4. render when a frame is owed or the document's schedule says one is due.
        const int64_t t = nowMs();
        const bool due = gDirty || (!paused && nextFrameAt != INT64_MAX && t >= nextFrameAt);
        if (due) {
            player.renderFrame();
            disp->present(player.pixels(), player.renderW(), player.renderH());
            gDirty = false;
            if (paused) {
                nextFrameAt = INT64_MAX;
            } else {
                int d = player.nextDelayMs();         // -1 static, 0 continuous, >0 timed
                nextFrameAt = d < 0 ? INT64_MAX : nowMs() + std::max(d, minFrameMs);
            }
        }

        // 5. wait briefly so the window stays responsive and console input wakes us.
        std::unique_lock<std::mutex> lk(gMx);
        gCv.wait_for(lk, std::chrono::milliseconds(16), [] { return !gQueue.empty(); });
    }

    tvg::Initializer::term();
    return 0;
}
