#include <cmath>
#include "../falsecolor_core.h"
#include "test_common.h"
static void expectBand(float y, int want, const char* what) {
    int b = fc::bandFromY(y);
    CHECK(b == want, "%s: Y=%.9g band %d want %d", what, y, b, want);
}
int main() {
    expectBand(0.0f, 0, "0");
    expectBand(16.0f / 255.0f, 0, "16/255");
    expectBand(17.0f / 255.0f, 1, "17/255");
    expectBand(237.0f / 255.0f, 13, "237/255");
    expectBand(238.0f / 255.0f, 14, "238/255");
    expectBand(1.0f, 14, "1.0");
    // Full 8-bit table (section 1), both directly and through gray pixels.
    const int lo[15] = {0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238};
    const int hi[15] = {16, 33, 50, 67, 84, 101, 118, 135, 152, 169, 186, 203, 220, 237, 255};
    for (int k = 0; k < 15; ++k)
        for (int c = lo[k]; c <= hi[k]; ++c) {
            float g = c / 255.0f;
            int b = fc::bandFromY(fc::lumaOf(g, g, g, fc::LUMA_REC709));
            int b2 = fc::bandFromY(fc::lumaOf(g, g, g, fc::LUMA_REC2020));
            CHECK(b == k && b2 == k, "code %d: band %d/%d want %d", c, b, b2, k);
        }
    // Palette header consistency: kPaletteF == kPalette255 / 255 exactly.
    for (int k = 0; k < 15; ++k)
        for (int c = 0; c < 3; ++c)
            CHECK(fc::kPaletteF[k][c] == fc::kPalette255[k][c] / 255.0f, "palette %d,%d out of sync with palette_generated.h", k, c);
    TEST_END();
}
