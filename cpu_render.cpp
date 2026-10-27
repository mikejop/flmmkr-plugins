#include "cpu_render.h"

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace fc {

FCPixel renderPixel(const float* src, const FCParams& p, const uint8_t* overlay, int gx, int gy) {
    const float* s = src + p.srcBase + (ptrdiff_t)gy * p.srcRow + gx * 4;
    FCPixel in{s[0], s[1], s[2], s[3]};
    FCPixel o = fc_shade(in, p.source, p.mat, p.matIdentity, p.mix);
    if (overlay) {
        int idx = fc_overlay_index(p.ovEnabled, p.ovX, p.ovY, p.ovW, p.ovH, p.frameX0, p.frameTop, p.winX0 + gx, p.winY0 + gy);
        if (idx >= 0) {
            const uint8_t* t = overlay + (size_t)idx * 4;
            o = fc_blend_overlay(o, t[0], t[1], t[2], t[3]);
        }
    }
    return o;
}

void cpuRender(const float* src, float* dst, const FCParams& p, const uint8_t* overlay) {
    if (p.w <= 0 || p.h <= 0) return;
    auto rows = [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* d = dst + p.dstBase + (ptrdiff_t)y * p.dstRow;
            for (int x = 0; x < p.w; ++x) {
                FCPixel o = renderPixel(src, p, overlay, x, y);
                d[x * 4] = o.r; d[x * 4 + 1] = o.g; d[x * 4 + 2] = o.b; d[x * 4 + 3] = o.a;
            }
        }
    };
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    int n = (int)std::min<unsigned>(hw, (unsigned)std::max(1, p.h / 8));
    if (n <= 1) { rows(0, p.h); return; }
    std::vector<std::thread> th;
    for (int i = 0; i < n; ++i)
        th.emplace_back(rows, (int)((long long)p.h * i / n), (int)((long long)p.h * (i + 1) / n));
    for (auto& t : th) t.join();
}

}  // namespace fc
