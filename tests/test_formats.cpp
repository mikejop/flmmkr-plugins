// The generic path (8/16-bit, float, RGB/RGBA, negative stride) matches the float RGBA path,
// including the scale overlay.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../cpu_render.h"
#include "../falsecolor_core.h"
#include "../overlay.h"
#include "test_common.h"
int main() {
    const int w = 640, h = 360;
    std::vector<float> img((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = (float)x / (w - 1);
            float* p = &img[((size_t)y * w + x) * 4];
            p[0] = v; p[1] = v * 0.9f; p[2] = v * (float)y / h; p[3] = 1.0f;
        }
    fc::ScaleOverlay ov = fc::buildScaleOverlay(w, h, true);
    FCParams p{};
    p.w = w; p.h = h; p.srcRow = p.dstRow = w * 4; p.frameTop = h; p.source = FC_SRC_REC709; p.mix = 1;
    bool ident = true;
    p.mat = fc::sourceToRec709(p.source, &ident);
    p.matIdentity = ident;
    p.ovEnabled = ov.valid(); p.ovX = ov.x; p.ovY = ov.y; p.ovW = ov.w; p.ovH = ov.h;
    const uint8_t* ovp = ov.valid() ? ov.rgba.data() : nullptr;

    for (int comps : {3, 4})
        for (int bpc : {1, 2, 4}) {
            const int pix = comps * bpc;
            std::vector<uint8_t> src((size_t)w * h * pix), dst(src.size());
            std::vector<float> deq(img.size()), expect(img.size());
            for (size_t i = 0; i < (size_t)w * h; ++i)
                for (int c = 0; c < 4; ++c) {
                    if (c == 3 && comps == 3) { deq[i * 4 + 3] = 1.0f; continue; }
                    float v = img[i * 4 + c];
                    uint8_t* q = &src[i * pix + c * bpc];
                    if (bpc == 1) { q[0] = (uint8_t)std::lround(v * 255); deq[i * 4 + c] = q[0] / 255.0f; }
                    else if (bpc == 2) { uint16_t u = (uint16_t)std::lround(v * 65535); std::memcpy(q, &u, 2); deq[i * 4 + c] = u / 65535.0f; }
                    else { std::memcpy(q, &v, 4); deq[i * 4 + c] = v; }
                }
            fc::cpuRender(deq.data(), expect.data(), p, ovp);
            fc::cpuRenderAny(src.data(), (ptrdiff_t)w * pix, dst.data(), (ptrdiff_t)w * pix, comps, bpc, p, ovp);
            const float tol = bpc == 1 ? 0.5f / 255 + 1e-5f : bpc == 2 ? 0.5f / 65535 + 1e-6f : 0.0f;
            int bad = 0;
            for (size_t i = 0; i < (size_t)w * h; ++i)
                for (int c = 0; c < 3; ++c) {
                    const uint8_t* q = &dst[i * pix + c * bpc];
                    float v;
                    if (bpc == 1) v = q[0] / 255.0f;
                    else if (bpc == 2) { uint16_t u; std::memcpy(&u, q, 2); v = u / 65535.0f; }
                    else std::memcpy(&v, q, 4);
                    float e = bpc == 4 ? expect[i * 4 + c] : std::fmin(1.0f, std::fmax(0.0f, expect[i * 4 + c]));
                    if (std::fabs(v - e) > tol) ++bad;
                }
            CHECK(bad == 0, "comps=%d bpc=%d: %d components off", comps, bpc, bad);
        }
    TEST_END();
}
