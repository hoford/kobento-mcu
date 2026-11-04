// The --list panel: a scrolling list of the documents, right of the player. Drawn with ThorVG into
// its own buffer, only when it changes, so scrolling doesn't re-rasterize the document.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tvg { class SwCanvas; }

namespace kobento {

class DocPanel {
public:
    // ratio: drawable px per window point, so rows keep their size on a HiDPI display.
    DocPanel(std::vector<std::string> rows, float ratio);
    ~DocPanel();

    void resize(int w, int h);
    void setCurrent(int row);            // highlight it and scroll it into view
    void scrollBy(float px);
    int rowAt(float y) const;            // panel px -> row, -1 past the end
    void draw();

    const uint32_t* pixels() const { return mPx.data(); }
    int width() const { return mW; }
    int height() const { return mH; }

private:
    void clampScroll();

    std::vector<std::string> mRows;
    std::vector<uint32_t> mPx;
    std::unique_ptr<tvg::SwCanvas> mCanvas;
    float mRowH, mFontPx, mPad, mScroll = 0;
    int mW = 0, mH = 0, mCurrent = -1;
};

}  // namespace kobento
