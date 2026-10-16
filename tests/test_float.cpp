#include <cmath>
#include <limits>
#include "../falsecolor_core.h"
#include "test_common.h"
static void px(float r, float g, float b, float a, float mix, float out[4]) {
    float in[4] = {r, g, b, a};
    fc::falseColorPixel(in, in, fc::LUMA_REC709, mix, out);
}
int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    float o[4];
    px(-0.5f, -0.5f, -0.5f, 1, 1, o);
    CHECK(o[0] == fc::kPaletteF[0][0] && o[2] == fc::kPaletteF[0][2], "negative -> band 0");
    px(1.5f, 1.5f, 1.5f, 1, 1, o);
    CHECK(o[0] == fc::kPaletteF[14][0] && o[1] == fc::kPaletteF[14][1], "1.5 -> band 14");
    px(100.0f, 100.0f, 100.0f, 1, 1, o);
    CHECK(o[0] == fc::kPaletteF[14][0], "100 -> band 14");
    px(nan, 0.5f, 0.5f, 0.7f, 1, o);
    CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0 && o[3] == 0.7f, "NaN -> black, alpha kept");
    px(inf, inf, inf, 1, 1, o);
    CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0, "Inf -> black");
    px(-inf, 0, 0, 1, 1, o);
    CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0, "-Inf -> black");
    px(nan, nan, nan, 1, 0.5f, o);
    for (int i = 0; i < 4; ++i) CHECK(std::isfinite(o[i]), "mix with NaN finite");
    px(0.3f, 0.3f, 0.3f, 0.25f, 0.0f, o);
    CHECK(o[0] == 0.3f && o[3] == 0.25f, "mix=0 returns input (no clamp before mix)");
    px(2.0f, 2.0f, 2.0f, 1, 0.0f, o);
    CHECK(o[0] == 2.0f, "mix=0 keeps >1 values intact");
    TEST_END();
}
