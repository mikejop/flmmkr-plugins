// Source curves and primaries: published reference values, mid grey placement, monotonic ramps.
#include <cmath>
#include <initializer_list>
#include "../falsecolor_core.h"
#include "test_common.h"
static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
int main() {
    // Decoders against published values for 18% grey.
    CHECK(near(fc_decode(FC_SRC_ACESCCT, 0.4135884f), 0.18f, 1e-4f), "ACEScct 0.4135884 -> 0.18 (got %g)", fc_decode(FC_SRC_ACESCCT, 0.4135884f));
    CHECK(near(fc_decode(FC_SRC_DWG_DI, 0.336043f), 0.18f, 1e-4f), "DaVinci Intermediate 0.336043 -> 0.18 (got %g)", fc_decode(FC_SRC_DWG_DI, 0.336043f));
    CHECK(near(fc_decode(FC_SRC_CINEON, 685.0f / 1023.0f), 1.0f, 1e-4f), "Cineon 685 -> 1.0 (got %g)", fc_decode(FC_SRC_CINEON, 685.0f / 1023.0f));
    CHECK(near(fc_decode(FC_SRC_CINEON, 95.0f / 1023.0f), 0.0f, 1e-4f), "Cineon 95 -> 0.0");
    CHECK(near(fc_decode(FC_SRC_ACESCCT, 0.0729055341958355f), 0.0f, 1e-6f), "ACEScct toe -> 0");
    CHECK(near(fc_oetf_bt709(0.18f), 0.4090f, 1e-3f), "BT.709 OETF(0.18) = %g", fc_oetf_bt709(0.18f));

    // AP1 -> Rec.709 (Bradford) against the commonly published ACEScg -> linear Rec.709 matrix.
    const float ap1[9] = {1.70505f, -0.62179f, -0.08326f, -0.13026f, 1.14080f, -0.01055f, -0.02400f, -0.12897f, 1.15297f};
    bool ident = true;
    FCMat3 m = fc::sourceToRec709(FC_SRC_ACESCCT, &ident);
    CHECK(!ident, "AP1 matrix is not identity");
    for (int i = 0; i < 9; ++i) CHECK(near(m.m[i], ap1[i], 2e-3f), "AP1->709 [%d] %g vs %g", i, m.m[i], ap1[i]);
    // Every matrix maps white to white (rows sum to 1).
    for (int s = 0; s < FC_NUM_SOURCES; ++s) {
        FCMat3 q = fc::sourceToRec709(s, &ident);
        for (int r = 0; r < 3; ++r) CHECK(near(q.m[r * 3] + q.m[r * 3 + 1] + q.m[r * 3 + 2], 1.0f, 1e-4f), "src %d row %d white", s, r);
    }
    m = fc::sourceToRec709(FC_SRC_REC709, &ident);
    CHECK(ident, "Rec.709 is identity");

    // Neutral 18% grey encoded in each scene-referred source lands in band 6 (green, block 7).
    struct { int src; float grey; } scene[] = {{FC_SRC_ACESCCT, 0.4135884f}, {FC_SRC_DWG_DI, 0.336043f}, {FC_SRC_CINEON, 0.0f}};
    {   // Cineon code value for 0.18
        const float black = 0.0108f;
        scene[2].grey = (685.0f + 300.0f * std::log10(0.18f * (1 - black) + black)) / 1023.0f;
    }
    for (auto& s : scene) {
        float y = fc::measureY(s.src, s.grey, s.grey, s.grey);
        CHECK(fc::bandFromY(y) == 6, "src %d: 18%% grey -> band %d (Y=%g), want 6", s.src, fc::bandFromY(y), y);
    }
    // Display-referred sources: grey stays grey and white stays 1.0.
    for (int s : {FC_SRC_REC709, FC_SRC_REC2020, FC_SRC_DWG_REC709}) {
        CHECK(near(fc::measureY(s, 0.5f, 0.5f, 0.5f), 0.5f, 1e-4f), "src %d grey 0.5 -> %g", s, fc::measureY(s, 0.5f, 0.5f, 0.5f));
        CHECK(fc::bandFromY(fc::measureY(s, 1, 1, 1)) == 14, "src %d white -> band 14", s);
    }
    CHECK(near(fc::measureY(FC_SRC_APPLE_G20, 0.5f, 0.5f, 0.5f), std::pow(0.25f, 1 / 2.4f), 1e-4f), "Apple Gamma 2.0 re-encoded to 2.4");

    // Every source: a gray ramp gives non-decreasing bands, no NaN, and reaches band 14.
    for (int s = 0; s < FC_NUM_SOURCES; ++s) {
        int prev = 0;
        for (int i = 0; i <= 2048; ++i) {
            float v = i / 2048.0f * 1.2f;
            float y = fc::measureY(s, v, v, v);
            CHECK(y >= 0.0f && y <= 1.0f, "src %d v=%g Y=%g out of range", s, v, y);
            int b = fc::bandFromY(y);
            CHECK(b >= prev, "src %d not monotonic at v=%g", s, v);
            prev = b;
        }
        CHECK(prev == 14, "src %d ramp tops at band %d", s, prev);
    }
    TEST_END();
}
