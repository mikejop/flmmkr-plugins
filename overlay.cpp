#include "overlay.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "falsecolor_core.h"
#include "font5x7.h"

namespace fc {

namespace {

struct Canvas {
    int w, h;
    std::vector<uint8_t>& px;

    // Straight-alpha "over".
    void blend(int x, int y, const uint8_t c[4]) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* d = &px[((size_t)y * w + x) * 4];
        float sa = c[3] / 255.0f, da = d[3] / 255.0f;
        float oa = sa + da * (1.0f - sa);
        for (int i = 0; i < 3; ++i) {
            float v = oa > 0 ? (c[i] * sa + d[i] * da * (1.0f - sa)) / oa : 0.0f;
            d[i] = (uint8_t)std::lround(std::min(255.0f, std::max(0.0f, v)));
        }
        d[3] = (uint8_t)std::lround(oa * 255.0f);
    }
    void rect(int x, int y, int rw, int rh, const uint8_t c[4]) {
        for (int j = std::max(0, y); j < std::min(h, y + rh); ++j)
            for (int i = std::max(0, x); i < std::min(w, x + rw); ++i) blend(i, j, c);
    }
    void frame(int x, int y, int rw, int rh, int t, const uint8_t c[4]) {
        rect(x, y, rw, t, c);
        rect(x, y + rh - t, rw, t, c);
        rect(x, y + t, t, rh - 2 * t, c);
        rect(x + rw - t, y + t, t, rh - 2 * t, c);
    }
};

std::vector<uint32_t> decodeUtf8(const char* s) {
    std::vector<uint32_t> out;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        if (*p < 0x80) out.push_back(*p++);
        else if ((*p & 0xE0) == 0xC0 && p[1]) { out.push_back(((p[0] & 0x1F) << 6) | (p[1] & 0x3F)); p += 2; }
        else ++p;  // unsupported sequence: skipped
    }
    return out;
}

int textWidth(const char* s, int scale) { return (int)decodeUtf8(s).size() * 6 * scale - scale; }

// Horizontal text; (x, y) = top-left of the 9-row cell.
void drawText(Canvas& cv, int x, int y, const char* s, int scale, const uint8_t c[4]) {
    for (uint32_t cp : decodeUtf8(s)) {
        if (const Glyph* g = findGlyph(cp))
            for (int r = 0; r < 9; ++r)
                for (int col = 0; col < 5; ++col)
                    if (g->rows[r][col] == '#') cv.rect(x + col * scale, y + r * scale, scale, scale, c);
        x += 6 * scale;
    }
}

// Text rotated 90 degrees counter-clockwise, read bottom to top; (x, bottom) = cell's left/bottom.
void drawTextVertical(Canvas& cv, int x, int bottom, const char* s, int scale, const uint8_t c[4]) {
    int i = 0;
    for (uint32_t cp : decodeUtf8(s)) {
        if (const Glyph* g = findGlyph(cp))
            for (int r = 0; r < 9; ++r)
                for (int col = 0; col < 5; ++col)
                    if (g->rows[r][col] == '#') cv.rect(x + r * scale, bottom - (i * 6 + col + 1) * scale, scale, scale, c);
        ++i;
    }
}

const char* const kTitle = "SISTEMA DE ZONAS";
const char* const kMidText = "TONS MÉDIOS";

}  // namespace

ScaleOverlay buildScaleOverlay(int frameW, int frameH, bool showLabels) {
    ScaleOverlay ov;
    if (frameW < 160 || frameH < 160) return ov;

    size_t maxLabel = 0;
    for (const char* l : kBandLabels) maxLabel = std::max(maxLabel, decodeUtf8(l).size());

    // Pick the largest pixel scale that fits (about 1 font pixel per 540 lines).
    for (int s = std::max(1, (int)std::lround(frameH / 540.0)); s >= 1; --s) {
        const int pad = 6 * s, gap = 4 * s;
        const int titleH = showLabels ? 9 * s + gap : 0;
        const int barW = 14 * s;
        const int numW = textWidth("102-109", s);
        const int vtextW = showLabels ? 9 * s : 0;
        const int labelW = showLabels ? (int)maxLabel * 6 * s : 0;
        int w = pad + barW + gap + numW + pad;
        if (showLabels) w += gap + vtextW + 2 * gap + labelW;
        w = std::max(w, showLabels ? textWidth(kTitle, s) + 2 * pad : 0);
        const int h = (int)std::lround(frameH * 0.86);
        const double bandH = (h - 2 * pad - titleH) / 15.0;
        if (w > frameW * 0.45 || bandH < 9 * s + 2 * s) continue;

        ov.w = w; ov.h = h;
        ov.x = std::max(8, (int)std::lround(frameW * 0.012));
        ov.y = (frameH - h) / 2;
        ov.rgba.assign((size_t)w * h * 4, 0);
        Canvas cv{w, h, ov.rgba};

        const uint8_t bg[4] = {12, 12, 14, 165}, white[4] = {255, 255, 255, 255}, dim[4] = {200, 200, 200, 255};
        cv.rect(0, 0, w, h, bg);
        if (showLabels) drawText(cv, pad, pad, kTitle, s, white);

        const int top = pad + titleH;
        const int barX = pad, numX = barX + barW + gap;
        auto bandTop = [&](int k) { return top + (int)std::lround((14 - k) * bandH); };
        auto bandBottom = [&](int k) { return top + (int)std::lround((15 - k) * bandH); };

        for (int k = 0; k < kNumBands; ++k) {
            const int y0 = bandTop(k), y1 = bandBottom(k);
            const uint8_t col[4] = {(uint8_t)kPalette255[k][0], (uint8_t)kPalette255[k][1], (uint8_t)kPalette255[k][2], 255};
            cv.rect(barX, y0 + s / 2, barW, std::max(1, y1 - y0 - s), col);
            const int ty = y0 + (y1 - y0 - 7 * s) / 2 - 2 * s;  // centre the 7 glyph rows
            const std::string range = std::to_string(bandIreLow(k)) + "-" + std::to_string(bandIreHigh(k));
            drawText(cv, numX, ty, range.c_str(), s, k >= kMidFirst && k <= kMidLast ? white : dim);
            if (showLabels) drawText(cv, numX + numW + gap + vtextW + 2 * gap, ty, kBandLabels[k], s, white);
        }

        // Mid-tone / skin zone (blocks 7-9 from the bottom): outline around their bars and numbers.
        const int midTop = bandTop(kMidLast), midBottom = bandBottom(kMidFirst);
        cv.frame(barX - 2 * s, midTop - s, barW + gap + numW + 4 * s, midBottom - midTop + 2 * s, s, white);
        if (showLabels) {  // "TONS MÉDIOS" written vertically right after the outline
            const int len = textWidth(kMidText, s);
            if (len <= midBottom - midTop)
                drawTextVertical(cv, numX + numW + gap, midBottom - (midBottom - midTop - len) / 2, kMidText, s, white);
        }
        return ov;
    }
    return ScaleOverlay{};
}

}  // namespace fc
