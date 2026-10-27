// Gray ramp 0..1 in 1024 steps (Rec.709 display): bands rise 0..14 without skipping.
#include "../falsecolor_core.h"
#include "test_common.h"
int main() {
    int prev = 0, first = -1, last = -1;
    for (int i = 0; i < 1024; ++i) {
        float v = (float)i / 1023.0f;
        float in[4] = {v, v, v, 1}, out[4];
        int b = fc::bandFromY(fc::measureY(FC_SRC_REC709, v, v, v));
        fc::falseColorPixel(in, FC_SRC_REC709, 1.0f, out);
        CHECK(b >= prev && b - prev <= 1, "i=%d band jumped %d->%d", i, prev, b);
        CHECK(out[0] == fc::kPaletteF[b][0] && out[1] == fc::kPaletteF[b][1] && out[2] == fc::kPaletteF[b][2], "i=%d palette", i);
        if (first < 0) first = b;
        last = b; prev = b;
    }
    CHECK(first == 0, "0.0 -> band %d", first);
    CHECK(last == 14, "1.0 -> band %d", last);
    TEST_END();
}
