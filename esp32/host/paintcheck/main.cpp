// paintcheck: drive the ThorVG backend (the boards' renderer) directly with paint features a
// JSON document can't easily express, and check the pixels.
//
//   paintcheck out.png [photo.jpg]
//
// Colour filters (Skia's SkColorFilters::Blend: the filter colour is the source, what the
// paint draws the destination) on solid rects, a gradient, text and a bitmap; and a JPEG
// decoded through loadBitmap (TYPE_PNG, as documents carry it). Prints the centre pixel of
// each solid swatch next to the value worked out by hand, and exits 1 on a mismatch.
#include "rccore/PaintBundle.h"
#include "rccore/RemoteContext.h"
#include "kobentovg/ThorvgPaintContext.h"

#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using rccore::PaintBundle;

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> c;
    be32(c, (uint32_t) data.size());
    c.insert(c.end(), type, type + 4);
    c.insert(c.end(), data.begin(), data.end());
    be32(c, (uint32_t) crc32(0, c.data() + 4, (uInt) (c.size() - 4)));
    fwrite(c.data(), 1, c.size(), f);
}

static bool writePng(const char* path, const uint32_t* px, int w, int h) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        for (int x = 0; x < w; x++) {
            uint32_t p = px[(size_t) y * w + x];
            uint32_t a = p >> 24, r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            if (a && a < 255) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; }
            raw.insert(raw.end(), {(uint8_t) r, (uint8_t) g, (uint8_t) b, (uint8_t) a});
        }
    }
    uLongf zlen = compressBound((uLong) raw.size());
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), (uLong) raw.size(), 6) != Z_OK) return false;
    z.resize(zlen);
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    be32(ihdr, w); be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(f, "IHDR", ihdr);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    fclose(f);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: paintcheck out.png [photo.jpg]\n"); return 2; }
    tvg::Initializer::init(0);
    std::vector<uint8_t> font = readFile(FONT_DIR "/Roboto-Bold.ttf");
    kobentovg::ThorvgPaintContext::registerFont(kobentovg::ThorvgPaintContext::Face::Sans, font.data(), font.size());

    const int W = 600, H = 420;
    std::vector<uint32_t> px((size_t) W * H);
    rccore::RemoteContext ctx;
    kobentovg::ThorvgPaintContext pc(ctx, px.data(), W, H);
    ctx.setPaintContext(&pc);
    pc.beginFrame({1, 0, 0, 0, 1, 0, 0, 0, 1}, 0xFFFFFFFF);

    auto paint = [&](uint32_t color, int filterMode = -1, uint32_t filter = 0) {
        PaintBundle p;
        p.addTag(PaintBundle::SHADER, 0);   // paint state carries over: drop any gradient
        p.addTag(PaintBundle::COLOR, (int) color);
        p.addUpperTag(PaintBundle::STYLE, PaintBundle::STYLE_FILL);
        if (filterMode >= 0) p.addTag(PaintBundle::COLOR_FILTER | (filterMode << 16), (int) filter);
        else p.addUpperTag(PaintBundle::CLEAR_COLOR_FILTER, 0);
        pc.applyPaint(p);
    };

    // Row 1: an orange swatch (#FF8000) through each filter. Expected: Skia's results.
    struct Case { const char* name; int mode; uint32_t filter, expect; };
    const Case cases[] = {
        {"none", -1, 0, 0xFFFF8000},
        {"SrcIn blue (tint)", PaintBundle::BLEND_MODE_SRC_IN, 0xFF2060FF, 0xFF2060FF},
        {"Multiply yellow", PaintBundle::BLEND_MODE_MULTIPLY, 0xFFFFFF00, 0xFFFF8000},
        {"Screen blue", PaintBundle::BLEND_MODE_SCREEN, 0xFF0000FF, 0xFFFF80FF},
        {"Darken grey", PaintBundle::BLEND_MODE_DARKEN, 0xFF606060, 0xFF606000},
        {"Modulate half", PaintBundle::BLEND_MODE_MODULATE, 0xFF808080, 0xFF804000},
        {"Dst (no change)", PaintBundle::BLEND_MODE_DST, 0xFF00FF00, 0xFFFF8000},
    };
    const int n = sizeof(cases) / sizeof(cases[0]);
    for (int i = 0; i < n; i++) {
        paint(0xFFFF8000, cases[i].mode, cases[i].filter);
        pc.drawRect(10 + i * 84, 10, 10 + i * 84 + 74, 84);
    }

    // Row 2: a linear gradient (red -> blue) without and with a SrcIn green filter.
    for (int k = 0; k < 2; k++) {
        PaintBundle p;
        p.addUpperTag(PaintBundle::STYLE, PaintBundle::STYLE_FILL);
        // GRADIENT: control word (2 colours), colours, 0 stops (even), x0 y0 x1 y1, tile mode.
        p.addUpperTag(PaintBundle::GRADIENT, PaintBundle::LINEAR_GRADIENT);
        auto& raw = const_cast<std::vector<int32_t>&>(p.getRawData());
        raw.push_back(2);
        raw.push_back((int32_t) 0xFFFF0000); raw.push_back((int32_t) 0xFF0000FF);
        raw.push_back(0);
        float x0 = 10 + k * 300, x1 = x0 + 280;
        for (float v : {x0, 100.0f, x1, 100.0f}) raw.push_back(PaintBundle::floatToIntBits(v));
        raw.push_back(0);
        if (k == 1) p.addTag(PaintBundle::COLOR_FILTER | (PaintBundle::BLEND_MODE_SRC_IN << 16), (int) 0xFF00A040);
        else p.addUpperTag(PaintBundle::CLEAR_COLOR_FILTER, 0);
        pc.applyPaint(p);
        pc.drawRect(x0, 100, x1, 160);
    }

    // Row 3: text, then the same through a SrcIn purple filter.
    paint(0xFF202020);
    pc.applyPaint([] { PaintBundle p; p.addTagFloat(PaintBundle::TEXT_SIZE, 40); return p; }());
    pc.loadText(1, "Plain");
    pc.drawTextAnchored(1, 10, 220, -1, 0, 0);
    paint(0xFF202020, PaintBundle::BLEND_MODE_SRC_IN, 0xFF9030C0);
    pc.applyPaint([] { PaintBundle p; p.addTagFloat(PaintBundle::TEXT_SIZE, 40); return p; }());
    pc.loadText(2, "Tinted");
    pc.drawTextAnchored(2, 300, 220, -1, 0, 0);

    // Row 4: a JPEG, decoded from TYPE_PNG data as documents carry it, then tinted.
    bool jpegOk = true;
    if (argc > 2) {
        std::vector<uint8_t> jpg = readFile(argv[2]);
        pc.loadBitmap(7, 0, 0, jpg);
        paint(0xFFFFFFFF);
        pc.drawBitmap(7, 10, 250, 290, 410);
        paint(0xFFFFFFFF, PaintBundle::BLEND_MODE_MULTIPLY, 0xFF40A0FF);
        pc.drawBitmap(7, 310, 250, 590, 410);
        // The picture must have drawn something other than the white background.
        jpegOk = false;
    }
    pc.endFrame();

    int bad = 0;
    for (int i = 0; i < n; i++) {
        uint32_t got = px[(size_t) 47 * W + 47 + i * 84];
        int dr = std::abs(int((got >> 16) & 0xFF) - int((cases[i].expect >> 16) & 0xFF));
        int dg = std::abs(int((got >> 8) & 0xFF) - int((cases[i].expect >> 8) & 0xFF));
        int db = std::abs(int(got & 0xFF) - int(cases[i].expect & 0xFF));
        bool ok = dr <= 2 && dg <= 2 && db <= 2;
        bad += !ok;
        std::printf("%-20s got %08X want %08X %s\n", cases[i].name, got, cases[i].expect, ok ? "ok" : "MISMATCH");
    }
    if (argc > 2) {
        for (int y = 260; y < 400 && !jpegOk; y += 7)
            for (int x = 20; x < 280; x += 7)
                if ((px[(size_t) y * W + x] & 0xFFFFFF) != 0xFFFFFF) { jpegOk = true; break; }
        std::printf("%-20s %s\n", "JPEG decoded", jpegOk ? "ok" : "NOTHING DRAWN");
        bad += !jpegOk;
    }
    writePng(argv[1], px.data(), W, H);
    return bad ? 1 : 0;
}
