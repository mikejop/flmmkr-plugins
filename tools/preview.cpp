// Renders a test frame through the CPU pipeline (with the scale) and writes a binary PPM.
// Usage: preview <out.ppm> <source> <labels 0|1> [w h]
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../cpu_render.h"
#include "../falsecolor_core.h"
#include "../overlay.h"
int main(int argc, char** argv) {
    if (argc < 4) return 1;
    int src = std::atoi(argv[2]), labels = std::atoi(argv[3]);
    int w = argc > 5 ? std::atoi(argv[4]) : 1920, h = argc > 5 ? std::atoi(argv[5]) : 1080;
    std::vector<float> img((size_t)w * h * 4), out(img.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {  // horizontal ramp 0..1 in the source's own encoding
            float v = (float)x / (w - 1);
            float* p = &img[((size_t)y * w + x) * 4];
            p[0] = p[1] = p[2] = v; p[3] = 1;
        }
    fc::ScaleOverlay ov = fc::buildScaleOverlay(w, h, labels != 0);
    FCParams p{};
    p.w = w; p.h = h; p.srcRow = p.dstRow = w * 4; p.frameTop = h; p.source = src; p.mix = 1;
    bool ident = true;
    p.mat = fc::sourceToRec709(src, &ident);
    p.matIdentity = ident;
    p.ovEnabled = ov.valid(); p.ovX = ov.x; p.ovY = ov.y; p.ovW = ov.w; p.ovH = ov.h;
    fc::cpuRender(img.data(), out.data(), p, ov.valid() ? ov.rgba.data() : nullptr);
    FILE* f = std::fopen(argv[1], "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y)  // OFX rows go bottom-up
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                float v = out[((size_t)y * w + x) * 4 + c];
                std::fputc((int)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0f + 0.5f), f);
            }
    std::fclose(f);
    return 0;
}
