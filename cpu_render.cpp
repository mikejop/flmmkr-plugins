#include "cpu_render.h"

#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

#include "falsecolor_core.h"

namespace fc {

namespace {
constexpr int kStrip = 16;  // rows converted per OCIO batch

void renderRows(const float* src, ptrdiff_t srcRow, float* dst, ptrdiff_t dstRow, int w, int y0,
                int y1, const RenderParams& p, const ConvertFn* conv) {
    std::vector<float> buf;
    if (conv) buf.resize((size_t)w * 4 * std::min(kStrip, y1 - y0));
    for (int ys = y0; ys < y1; ys += kStrip) {
        const int rows = std::min(kStrip, y1 - ys);
        if (conv) {
            for (int r = 0; r < rows; ++r)
                std::memcpy(&buf[(size_t)r * w * 4], src + (ptrdiff_t)(ys + r) * srcRow,
                            (size_t)w * 4 * sizeof(float));
            (*conv)(buf.data(), (size_t)w * rows);  // one batch, never per pixel
        }
        for (int r = 0; r < rows; ++r) {
            const float* s = src + (ptrdiff_t)(ys + r) * srcRow;
            const float* c = conv ? &buf[(size_t)r * w * 4] : s;
            float* d = dst + (ptrdiff_t)(ys + r) * dstRow;
            for (int x = 0; x < w; ++x) {
                if (!p.enable) {
                    std::memcpy(d + x * 4, s + x * 4, 4 * sizeof(float));
                    continue;
                }
                falseColorPixel(s + x * 4, c + x * 4, p.lumaMode, p.mix, d + x * 4);
            }
        }
    }
}
}  // namespace

void cpuRender(const float* src, ptrdiff_t srcRow, float* dst, ptrdiff_t dstRow, int w, int h,
               const RenderParams& p, const ConvertFn* conv) {
    if (w <= 0 || h <= 0) return;
    const bool needConv = p.enable && conv && *conv;
    const ConvertFn* cv = needConv ? conv : nullptr;
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    int n = (int)std::min<unsigned>(hw, (unsigned)std::max(1, h / 8));
    if (n <= 1) {
        renderRows(src, srcRow, dst, dstRow, w, 0, h, p, cv);
        return;
    }
    std::vector<std::thread> th;
    for (int i = 0; i < n; ++i) {
        int y0 = (int)((long long)h * i / n), y1 = (int)((long long)h * (i + 1) / n);
        th.emplace_back([=, &p] { renderRows(src, srcRow, dst, dstRow, w, y0, y1, p, cv); });
    }
    for (auto& t : th) t.join();
}

}  // namespace fc
