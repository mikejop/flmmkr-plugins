#include "falsecolor_core.h"

#include <cmath>

namespace fc {

const int kPalette255[kNumBands][3] = {
    {74, 44, 85},    //  0 purple
    {61, 98, 137},   //  1 blue
    {82, 130, 160},  //  2 steel blue
    {107, 160, 174}, //  3 teal
    {128, 163, 168}, //  4 grey-blue
    {142, 154, 132}, //  5 greyish olive
    {146, 185, 106}, //  6 green
    {160, 171, 159}, //  7 greyish green
    {211, 149, 144}, //  8 dark pink
    {218, 179, 175}, //  9 light pink
    {222, 228, 212}, // 10 greenish white
    {241, 242, 154}, // 11 pale yellow
    {254, 249, 122}, // 12 yellow
    {235, 179, 89},  // 13 orange
    {223, 88, 63},   // 14 red-orange
};

#ifdef FC_GENERATOR
// The generator tool derives the header from the ints above.
const float kPaletteF[kNumBands][3] = {};
#else
#include "palette_generated.h"
const float kPaletteF[kNumBands][3] = FC_PALETTE_INIT;
#endif

float lumaOf(float r, float g, float b, int lumaMode) {
    if (lumaMode == LUMA_REC2020) return 0.2627f * r + 0.6780f * g + 0.0593f * b;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

int bandFromY(float y) {
    if (!(y > 0.0f)) return 0;  // also NaN
    if (y > 1.0f) y = 1.0f;
    int b = (int)std::floor(y * 15.0f + kBandEps);
    return b > 14 ? 14 : (b < 0 ? 0 : b);
}

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void falseColorPixel(const float in[4], const float conv[3], int lumaMode, float mix, float out[4]) {
    const bool convOk = std::isfinite(conv[0]) && std::isfinite(conv[1]) && std::isfinite(conv[2]);
    float fcol[3] = {0.0f, 0.0f, 0.0f};  // NaN/Inf -> black
    if (convOk) {
        float y = lumaOf(clamp01(conv[0]), clamp01(conv[1]), clamp01(conv[2]), lumaMode);
        const float* p = kPaletteF[bandFromY(y)];
        fcol[0] = p[0]; fcol[1] = p[1]; fcol[2] = p[2];
    }
    for (int c = 0; c < 3; ++c) {
        float v = std::isfinite(in[c]) ? in[c] : 0.0f;
        out[c] = v * (1.0f - mix) + fcol[c] * mix;  // exact at mix=0 and mix=1
    }
    out[3] = std::isfinite(in[3]) ? in[3] : 1.0f;
}

}  // namespace fc
