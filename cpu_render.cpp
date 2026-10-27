#include "cpu_render.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

namespace fc {

namespace {
void parallelRows(int h, const std::function<void(int, int)>& fn) {
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    int n = (int)std::min<unsigned>(hw, (unsigned)std::max(1, h / 8));
    if (n <= 1) { fn(0, h); return; }
    std::vector<std::thread> th;
    for (int i = 0; i < n; ++i) th.emplace_back(fn, (int)((long long)h * i / n), (int)((long long)h * (i + 1) / n));
    for (auto& t : th) t.join();
}
float readComp(const uint8_t* p, int bpc) {
    if (bpc == 1) return p[0] / 255.0f;
    if (bpc == 2) { uint16_t v; std::memcpy(&v, p, 2); return v / 65535.0f; }
    float v; std::memcpy(&v, p, 4); return v;
}
void writeComp(uint8_t* p, int bpc, float v) {
    if (bpc == 4) { std::memcpy(p, &v, 4); return; }
    if (!(v > 0.0f)) v = 0.0f;  // also NaN
    if (v > 1.0f) v = 1.0f;
    if (bpc == 1) { p[0] = (uint8_t)std::lround(v * 255.0f); return; }
    uint16_t s = (uint16_t)std::lround(v * 65535.0f);
    std::memcpy(p, &s, 2);
}
FCPixel withOverlay(FCPixel o, const FCParams& p, const uint8_t* overlay, int gx, int gy) {
    if (!overlay) return o;
    int idx = fc_overlay_index(p.ovEnabled, p.ovX, p.ovY, p.ovW, p.ovH, p.frameX0, p.frameTop, p.winX0 + gx, p.winY0 + gy);
    if (idx < 0) return o;
    const uint8_t* t = overlay + (size_t)idx * 4;
    return fc_blend_overlay(o, t[0], t[1], t[2], t[3]);
}
}  // namespace

FCPixel renderPixel(const float* src, const FCParams& p, const uint8_t* overlay, int gx, int gy) {
    const float* s = src + p.srcBase + (ptrdiff_t)gy * p.srcRow + gx * 4;
    FCPixel in{s[0], s[1], s[2], s[3]};
    return withOverlay(fc_shade(in, p.source, p.mat, p.matIdentity, p.mix), p, overlay, gx, gy);
}

void cpuRender(const float* src, float* dst, const FCParams& p, const uint8_t* overlay) {
    if (p.w <= 0 || p.h <= 0) return;
    parallelRows(p.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* d = dst + p.dstBase + (ptrdiff_t)y * p.dstRow;
            for (int x = 0; x < p.w; ++x) {
                FCPixel o = renderPixel(src, p, overlay, x, y);
                d[x * 4] = o.r; d[x * 4 + 1] = o.g; d[x * 4 + 2] = o.b; d[x * 4 + 3] = o.a;
            }
        }
    });
}

void cpuRenderAny(const uint8_t* srcWin, ptrdiff_t srcRowBytes, uint8_t* dstWin, ptrdiff_t dstRowBytes,
                  int components, int bpc, const FCParams& p, const uint8_t* overlay) {
    if (p.w <= 0 || p.h <= 0 || (components != 3 && components != 4) || (bpc != 1 && bpc != 2 && bpc != 4)) return;
    const int pixBytes = components * bpc;
    parallelRows(p.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uint8_t* s = srcWin + (ptrdiff_t)y * srcRowBytes;
            uint8_t* d = dstWin + (ptrdiff_t)y * dstRowBytes;
            for (int x = 0; x < p.w; ++x) {
                const uint8_t* sp = s + (ptrdiff_t)x * pixBytes;
                FCPixel in{readComp(sp, bpc), readComp(sp + bpc, bpc), readComp(sp + 2 * bpc, bpc),
                           components == 4 ? readComp(sp + 3 * bpc, bpc) : 1.0f};
                FCPixel o = withOverlay(fc_shade(in, p.source, p.mat, p.matIdentity, p.mix), p, overlay, x, y);
                uint8_t* dp = d + (ptrdiff_t)x * pixBytes;
                writeComp(dp, bpc, o.r); writeComp(dp + bpc, bpc, o.g); writeComp(dp + 2 * bpc, bpc, o.b);
                if (components == 4) writeComp(dp + 3 * bpc, bpc, o.a);
            }
        }
    });
}

}  // namespace fc
