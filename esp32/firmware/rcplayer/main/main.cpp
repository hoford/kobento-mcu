// Kobento RC player — a minimal sample app.
//
// kobento-mcu is a library for *displaying* RemoteCompose documents. This app is the smallest
// thing that exercises it on an ESP32 board: it embeds the documents in documents/ (and
// documents/fit/, documents/3d/), shows the first one, and lets you step through the rest with
// the board's button or the serial console. No networking — document delivery (kobento-net)
// and integrations (kobento-mcuApps) live in their own repositories.
//
// Documents drive the player with hostAction(0, "name[=value]") (docs/RC_ACTION_PATTERNS.md):
//   system.backlight=<0..1>|cycle   doc.show=<name>   doc.next   doc.back
// A document that declares the named variables Control.next / Control.back receives the board's
// buttons as press counts (instead of them stepping through the documents).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "esp_log.h"
#include "sdkconfig.h"

#include "rcstage/stage.h"
#include "rcstage/device_state.h"
#include "board/board.h"

#include "console.h"
#include "embedded_docs.h"

static const char* TAG = "rcplayer";

static constexpr int kDocs = sizeof(kEmbeddedDocs) / sizeof(kEmbeddedDocs[0]);
static int sCurrent = 0, sPrevious = 0;

// A document may come in variants for particular screens, shown wherever <name> is asked for:
// "<name>_round" on a round screen, "<name>_light" (a white palette) on e-ink.
struct Variant { const char* suffix; bool mine; };
static constexpr Variant kVariants[] = {{"_round", board::kRound}, {"_light", board::kEink}};

static int findExact(const std::string& name) {
    for (int i = 0; i < kDocs; i++)
        if (name == kEmbeddedDocs[i].name) return i;
    return -1;
}
static int findDoc(const std::string& name) {
    for (const Variant& v : kVariants) {
        if (!v.mine) continue;
        int i = findExact(name + v.suffix);
        if (i >= 0) return i;
    }
    return findExact(name);
}
// The step cycle shows each document once, as this board opens it: it skips the variants meant
// for other screens, and the plain document when this board has a variant.
static bool otherScreen(int i) {
    std::string n = kEmbeddedDocs[i].name;
    for (const Variant& v : kVariants) {
        const size_t len = strlen(v.suffix);
        if (n.size() > len && n.compare(n.size() - len, len, v.suffix) == 0) {
            n.resize(n.size() - len);
            break;
        }
    }
    return findDoc(n) != i;
}

// Player task: open embedded document i, skipping any that fail. `step` (+1 or -1) is the
// direction to skip in when i is a variant for another screen; 0 opens i as asked.
static void open(int i, int step = 0) {
    const int from = sCurrent;
    for (int tries = 0; tries < kDocs; tries++, i += step ? step : 1) {
        const int at = ((i % kDocs) + kDocs) % kDocs;
        if (step && otherScreen(at)) continue;
        sCurrent = at;
        const auto& d = kEmbeddedDocs[sCurrent];
        if (rcstage::load(d.start, d.end - d.start, d.name, d.fit)) {
            if (sCurrent != from) sPrevious = from;
            return;
        }
    }
}

// ── Buttons and host actions (player task) ───────────────────────────────────────────────

static int sNextPresses = 0, sBackPresses = 0;
static int sBacklight = 100;

// The board's buttons go to the document when it declares Control.next / .back, else they step
// through the documents.
static void button(int step) {
    if (!rcstage::declaresNamed("Control.next") && !rcstage::declaresNamed("Control.back")) {
        open(sCurrent + step, step);
        return;
    }
    if (step > 0) rcstage::setNamed("Control.next", std::to_string(++sNextPresses));
    else rcstage::setNamed("Control.back", std::to_string(++sBackPresses));
}

static void runHostAction(const std::string& name, const std::string& value) {
    if (name == "system.backlight") {
        // A fraction 0..1, or "cycle" through 100 -> 60 -> 30 %.
        int pct = value == "cycle" ? (sBacklight > 60 ? 60 : sBacklight > 30 ? 30 : 100)
                                   : (int) (std::strtof(value.c_str(), nullptr) * 100);
        sBacklight = std::max(5, std::min(100, pct));
        board::setBrightness(sBacklight);
        rcstage::setNamed("system.backlight", std::to_string(sBacklight / 100.0f));
    } else if (name == "doc.show") {
        int i = findDoc(value);
        if (i >= 0) open(i);
        else ESP_LOGW(TAG, "no document '%s'", value.c_str());
    } else if (name == "doc.next") {
        open(sCurrent + 1, 1);
    } else if (name == "doc.back") {
        open(sPrevious);
    } else {
        ESP_LOGW(TAG, "unknown host action '%s'", name.c_str());
    }
}

static void tick() {
    console::Request req;
    while (console::poll(req)) {
        switch (req.cmd) {
            case console::Cmd::Snap:
                board::dumpFrame(board::kWidth > 400 ? 2 : 1);   // ~360 px wide
                break;
            case console::Cmd::Res: rcstage::setRenderDiv(req.arg); break;
            case console::Cmd::Next: open(sCurrent + 1, 1); break;
            case console::Cmd::Prev: open(sCurrent - 1, -1); break;
            case console::Cmd::Doc: open(req.arg); break;
            case console::Cmd::List:
                for (int i = 0; i < kDocs; i++)
                    printf("%c %d %s\n", i == sCurrent ? '*' : ' ', i, kEmbeddedDocs[i].name);
                break;
            case console::Cmd::Stats: rcstage::requestStats(); break;
            case console::Cmd::Bench: rcstage::bench(); break;
            case console::Cmd::Act: {
                std::string md = req.text;
                size_t eq = md.find('=');
                printf("act %s\n", md.c_str());
                runHostAction(md.substr(0, eq), eq == std::string::npos ? "" : md.substr(eq + 1));
                break;
            }
            default: break;
        }
    }
    if (int step = board::buttonPressed()) button(step);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Kobento RC player on %s", board::kName);
    console::start();
    if (!board::displayInit()) return;
    rcstage::Hooks hooks;
    hooks.tick = tick;
    hooks.longPress = [] { open(sCurrent + 1, 1); };
    hooks.hostAction = [](int id, const std::string& md) {
        if (id != 0) return;
        size_t eq = md.find('=');
        runHostAction(md.substr(0, eq), eq == std::string::npos ? "" : md.substr(eq + 1));
    };
    hooks.namedAction = [](const std::string& name, int, const std::string& value) {
        runHostAction(name, value);
    };
    // Start on the Kobento splash if it is built in, else the first document.
    sCurrent = sPrevious = std::max(0, findDoc("kobento_splash"));
    const auto& d = kEmbeddedDocs[sCurrent];
    rcstage::start(hooks, d.start, d.end - d.start, d.name);
    rcstage::deviceStateStart();   // battery / memory / clock as system variables, for the docs
}
