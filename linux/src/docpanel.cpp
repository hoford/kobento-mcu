#include "docpanel.hpp"

#include <thorvg.h>

#include <algorithm>

namespace kobento {

static constexpr float kPxToTvgSize = 0.75f;   // as in ThorvgPaintContext
static const char* kFont = "rc-sans";          // registered by ThorvgPaintContext::registerFont

DocPanel::DocPanel(std::vector<std::string> rows, float ratio)
    : mRows(std::move(rows)), mRowH(26 * ratio), mFontPx(14 * ratio), mPad(10 * ratio) {}

DocPanel::~DocPanel() = default;

void DocPanel::resize(int w, int h) {
    mW = std::max(1, w);
    mH = std::max(1, h);
    mPx.assign((size_t) mW * mH, 0);
    mCanvas.reset(tvg::SwCanvas::gen());
    mCanvas->target(mPx.data(), mW, mW, mH, tvg::ColorSpace::ARGB8888);
    clampScroll();
    setCurrent(mCurrent);
}

void DocPanel::clampScroll() {
    const float maxScroll = std::max(0.f, mRows.size() * mRowH - mH);
    mScroll = std::max(0.f, std::min(maxScroll, mScroll));
}

void DocPanel::setCurrent(int row) {
    mCurrent = row;
    if (row < 0) return;
    const float top = row * mRowH;
    if (top < mScroll) mScroll = top;
    else if (top + mRowH > mScroll + mH) mScroll = top + mRowH - mH;
    clampScroll();
}

void DocPanel::scrollBy(float px) {
    mScroll += px;
    clampScroll();
}

int DocPanel::rowAt(float y) const {
    const int row = (int) ((y + mScroll) / mRowH);
    return y >= 0 && row >= 0 && row < (int) mRows.size() ? row : -1;
}

void DocPanel::draw() {
    if (!mCanvas) return;
    mCanvas->remove();
    auto rect = [&](float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b) {
        auto s = tvg::Shape::gen();
        s->appendRect(x, y, w, h);
        s->fill(r, g, b, 255);
        mCanvas->add(s);
    };
    rect(0, 0, (float) mW, (float) mH, 0x1E, 0x22, 0x27);

    const int first = (int) (mScroll / mRowH);
    const int last = std::min((int) mRows.size(), (int) ((mScroll + mH) / mRowH) + 1);
    for (int i = first; i < last; i++) {
        const float y = i * mRowH - mScroll;
        const bool current = i == mCurrent;
        if (current) rect(0, y, (float) mW, mRowH, 0x2F, 0x6F, 0xB5);
        auto t = tvg::Text::gen();
        t->font(kFont);
        t->size(mFontPx * kPxToTvgSize);
        t->text(mRows[i].c_str());
        if (current) t->fill(255, 255, 255);
        else t->fill(0xC8, 0xCE, 0xD4);
        t->translate(mPad, y + (mRowH - mFontPx * 1.2f) * 0.5f);
        mCanvas->add(t);
    }

    const float content = mRows.size() * mRowH;
    if (content > mH) {   // scroll thumb
        const float barW = mPad * 0.4f, barH = std::max(mRowH, mH * mH / content);
        rect(mW - barW, (mH - barH) * mScroll / (content - mH), barW, barH, 0x6B, 0x74, 0x7D);
    }
    mCanvas->draw(false);
    mCanvas->sync();
}

}  // namespace kobento
