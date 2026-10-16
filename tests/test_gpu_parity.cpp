// CPU (falseColorPixel via cpuRender) vs Metal kernel on a float32 image.
#include <cmath>
#include <limits>
#include <vector>
#include "../cpu_render.h"
#include "../falsecolor_core.h"
#include "../metal_backend.h"
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
    const float edgeV[] = {16 / 255.f, 17 / 255.f, 237 / 255.f, 238 / 255.f, 1.0f, 1.5f, -0.2f};
    for (float v : edgeV) push(v, v, v);
    push(nan, 0.5f, 0.5f);
    size_t n = img.size() / 4;
    // pad to a rectangle
    int w = 256, h = (int)((n + w - 1) / w);
    img.resize((size_t)w * h * 4, 0.25f);

    int failures = 0, nearEdge = 0;
    for (int luma = 0; luma < 2; ++luma) {
        for (float mix : {1.0f, 0.6f}) {
            fc::RenderParams p; p.lumaMode = luma; p.mix = mix;
            std::vector<float> ref(img.size());
            fc::cpuRender(img.data(), w * 4, ref.data(), w * 4, w, h, p, nullptr);

            void* sb = mr.stage(0, img.size()); void* db = mr.stage(1, img.size());
            std::copy(img.begin(), img.end(), mr.stagePtr(0));
            fc::MetalArgs a; a.w = w; a.h = h; a.srcRow = a.convRow = a.dstRow = w * 4; a.lumaMode = luma; a.mix = mix;
            if (!mr.run(sb, sb, db, a)) { std::printf("Metal run failed: %s\n", mr.lastError().c_str()); return 1; }
            const float* gpu = mr.stagePtr(1);
            for (size_t i = 0; i < (size_t)w * h; ++i) {
                const float* in = &img[i * 4];
                float y = fc::lumaOf(std::fmin(std::fmax(in[0], 0.f), 1.f), std::fmin(std::fmax(in[1], 0.f), 1.f), std::fmin(std::fmax(in[2], 0.f), 1.f), luma);
                bool edge = std::isfinite(in[0]) && std::fabs(y * 15.0f - std::round(y * 15.0f)) < 1e-5f;
                float maxd = 0;
                for (int c = 0; c < 4; ++c) maxd = std::fmax(maxd, std::fabs(ref[i * 4 + c] - gpu[i * 4 + c]));
                if (maxd >= 1.0f / 255.0f) {
                    if (edge) { ++nearEdge; std::printf("note: pixel %zu within 1e-5 of a band limit, diff %.4f (not a failure)\n", i, maxd); }
                    else { ++failures; if (failures < 10) std::printf("MISMATCH pixel %zu luma=%d mix=%.1f diff=%.5f\n", i, luma, mix, maxd); }
                }
            }
        }
    }
    CHECK(failures == 0, "%d CPU/GPU mismatches", failures);
    std::printf("near-edge differences logged: %d\n", nearEdge);
    TEST_END();
#endif
}
