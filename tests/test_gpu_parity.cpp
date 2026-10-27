// CPU (fc_shared.h via cpuRender) vs Metal kernel, every source, with the scale overlay.
// Band must match except for pixels within 1e-5 of a band limit (logged, not failed); RGB diff < 1/255.
#include <cmath>
#include <limits>
#include <vector>
#include "../cpu_render.h"
#include "../falsecolor_core.h"
#include "../metal_backend.h"
#include "../overlay.h"
#include "test_common.h"
int main() {
#if !defined(__APPLE__)
    TEST_SKIP("Metal only on macOS");
#else
    if (!fc::MetalRunner::available()) TEST_SKIP("no Metal device on this machine");
    fc::MetalRunner mr(nullptr);
    if (!mr.ok()) { std::printf("Metal init failed: %s\n", mr.lastError().c_str()); return 1; }
    std::printf("Metal device: %s\n", mr.deviceName().c_str());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> img;  // RGBA
    auto push = [&](float r, float g, float b) { img.insert(img.end(), {r, g, b, 0.5f}); };
    for (int i = 0; i < 4096; ++i) { float v = i / 4095.0f; push(v, v, v); }
    for (int i = 0; i < 1536; ++i) {  // saturated colour ramps
        float t = (i % 256) / 255.0f; int s = i / 256;
        float c[6][3] = {{1, t, 0}, {1 - t, 1, 0}, {0, 1, t}, {0, 1 - t, 1}, {t, 0, 1}, {1, 0, 1 - t}};
        push(c[s][0], c[s][1], c[s][2]);
    }
    for (float v : {16 / 255.f, 17 / 255.f, 237 / 255.f, 238 / 255.f, 1.0f, 1.5f, -0.2f}) push(v, v, v);
    push(nan, 0.5f, 0.5f);
    const int w = 512;
    const int h = (int)((img.size() / 4 + w - 1) / w) + 300;  // extra rows so the overlay fits
    img.resize((size_t)w * h * 4, 0.25f);
    fc::ScaleOverlay ov = fc::buildScaleOverlay(w, h, true);

    int failures = 0, nearEdge = 0;
    unsigned gen = 0;
    for (int src = 0; src < FC_NUM_SOURCES; ++src)
        for (float mix : {1.0f, 0.6f}) {
            FCParams p{};
            p.w = w; p.h = h; p.srcRow = p.dstRow = w * 4;
            p.frameTop = h; p.source = src; p.mix = mix;
            bool ident = true;
            p.mat = fc::sourceToRec709(src, &ident);
            p.matIdentity = ident;
            p.ovEnabled = ov.valid(); p.ovX = ov.x; p.ovY = ov.y; p.ovW = ov.w; p.ovH = ov.h;
            std::vector<float> ref(img.size());
            fc::cpuRender(img.data(), ref.data(), p, ov.valid() ? ov.rgba.data() : nullptr);

            void* sb = mr.stage(0, img.size());
            void* db = mr.stage(1, img.size());
            std::copy(img.begin(), img.end(), mr.stagePtr(0));
            mr.setOverlay(ov.rgba.data(), ov.rgba.size(), ++gen);
            if (!mr.run(sb, db, p)) { std::printf("Metal run failed: %s\n", mr.lastError().c_str()); return 1; }
            const float* gpu = mr.stagePtr(1);
            for (size_t i = 0; i < (size_t)w * h; ++i) {
                const float* in = &img[i * 4];
                float y = fc::measureY(src, in[0], in[1], in[2]);
                bool edge = y >= 0 && std::fabs(y * 15.0f - std::round(y * 15.0f)) < 15e-5f;  // |Y - k/15| < 1e-5
                float maxd = 0;
                for (int c = 0; c < 4; ++c) maxd = std::fmax(maxd, std::fabs(ref[i * 4 + c] - gpu[i * 4 + c]));
                if (maxd >= 1.0f / 255.0f) {
                    if (edge) { ++nearEdge; std::printf("note: src %d pixel %zu within 1e-5 of a band limit (not a failure)\n", src, i); }
                    else if (++failures < 10) std::printf("MISMATCH src %d mix %.1f pixel %zu diff %.5f\n", src, mix, i, maxd);
                }
            }
        }
    CHECK(failures == 0, "%d CPU/GPU mismatches", failures);
    std::printf("near-edge differences logged: %d\n", nearEdge);
    TEST_END();
#endif
}
