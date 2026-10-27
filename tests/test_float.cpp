// Negative, >1, NaN and Inf values never break the pipeline, for every source.
#include <cmath>
#include <limits>
#include "../falsecolor_core.h"
#include "test_common.h"
static void px(int src, float r, float g, float b, float a, float mix, float out[4]) {
    float in[4] = {r, g, b, a};
    fc::falseColorPixel(in, src, mix, out);
}
static bool isBand(const float o[4], int k) { return o[0] == fc::kPaletteF[k][0] && o[1] == fc::kPaletteF[k][1] && o[2] == fc::kPaletteF[k][2]; }
int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    float o[4];
    for (int src = 0; src < FC_NUM_SOURCES; ++src) {
        px(src, -0.5f, -0.5f, -0.5f, 1, 1, o);
        CHECK(isBand(o, 0), "src %d: negative -> band 0", src);
        px(src, 1.5f, 1.5f, 1.5f, 1, 1, o);
        CHECK(isBand(o, 14), "src %d: 1.5 -> band 14", src);
        px(src, 100.0f, 100.0f, 100.0f, 1, 1, o);
        CHECK(isBand(o, 14), "src %d: 100 -> band 14", src);
        px(src, nan, 0.5f, 0.5f, 0.7f, 1, o);
        CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0 && o[3] == 0.7f, "src %d: NaN -> black, alpha kept", src);
        px(src, inf, inf, inf, 1, 1, o);
        CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0, "src %d: Inf -> black", src);
        px(src, nan, nan, nan, 1, 0.5f, o);
        for (int i = 0; i < 4; ++i) CHECK(std::isfinite(o[i]), "src %d: mix with NaN finite", src);
        px(src, 2.0f, 0.3f, 0.3f, 0.25f, 0.0f, o);
        CHECK(o[0] == 2.0f && o[1] == 0.3f && o[3] == 0.25f, "src %d: mix=0 returns input untouched", src);
    }
    TEST_END();
}
