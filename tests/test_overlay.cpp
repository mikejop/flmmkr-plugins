// Scale overlay: layout, palette blocks, mid-tone outline, label switch, small frames.
#include <utility>
#include <cstdio>
#include "../falsecolor_core.h"
#include "../overlay.h"
#include "test_common.h"
static const uint8_t* at(const fc::ScaleOverlay& o, int x, int y) { return &o.rgba[((size_t)y * o.w + x) * 4]; }
static bool findColor(const fc::ScaleOverlay& o, int k) {
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x) {
            const uint8_t* p = at(o, x, y);
            if (p[0] == fc::kPalette255[k][0] && p[1] == fc::kPalette255[k][1] && p[2] == fc::kPalette255[k][2] && p[3] == 255) return true;
        }
    return false;
}
int main() {
    for (int labels = 0; labels < 2; ++labels)
        for (auto sz : {std::pair<int, int>{1920, 1080}, {3840, 2160}, {1280, 720}, {4096, 1716}, {1080, 1920}}) {
            fc::ScaleOverlay o = fc::buildScaleOverlay(sz.first, sz.second, labels != 0);
            CHECK(o.valid(), "%dx%d labels=%d: overlay built", sz.first, sz.second, labels);
            if (!o.valid()) continue;
            CHECK(o.x >= 0 && o.y >= 0 && o.x + o.w <= sz.first && o.y + o.h <= sz.second, "%dx%d inside frame", sz.first, sz.second);
            CHECK(o.w <= sz.first * 0.45, "%dx%d width %d", sz.first, sz.second, o.w);
            CHECK(o.x < sz.first / 10, "%dx%d on the left (x=%d)", sz.first, sz.second, o.x);
            for (int k = 0; k < 15; ++k) CHECK(findColor(o, k), "%dx%d band %d colour present", sz.first, sz.second, k);
            std::printf("%dx%d labels=%d -> overlay %dx%d at (%d,%d)\n", sz.first, sz.second, labels, o.w, o.h, o.x, o.y);
        }
    fc::ScaleOverlay on = fc::buildScaleOverlay(1920, 1080, true), off = fc::buildScaleOverlay(1920, 1080, false);
    CHECK(on.w > off.w, "labels make the panel wider (%d vs %d)", on.w, off.w);
    CHECK(!fc::buildScaleOverlay(100, 80, true).valid(), "tiny frame -> no overlay");
    TEST_END();
}
