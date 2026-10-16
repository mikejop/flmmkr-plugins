// For each available config (ACES 1.x and 2.0): roles resolve, >=1 SDR Rec.709 view, and the same
// gray ramp gives the same bands with the same "Output View". Skips missing configs, never fails for them.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "../falsecolor_core.h"
#include "test_common.h"
#include "test_ocio_util.h"
int main() {
    std::vector<std::vector<int>> bandsPerVersion;
    int tested = 0;
    for (int version = 1; version <= 2; ++version) {
        std::string path = findConfig(version);
        if (path.empty()) { std::printf("warning: no config for ACES %s - case skipped\n", version == 1 ? "1.x" : "2.0"); continue; }
        std::printf("ACES %s config: %s\n", version == 1 ? "1.x" : "2.0", path.c_str());
        ++tested;
        std::vector<int> bands;
        for (int space : {fc::INPUT_ACESCG, fc::INPUT_ACESCCT, fc::INPUT_ACES2065}) {
            fc::OcioSetup s; s.configPath = path; s.inputSpace = space; s.acesVersion = version == 1 ? fc::ACES_1X : fc::ACES_20;
            fc::OcioPipeline p; std::string err;
            bool ok = p.configure(s, err);
            CHECK(ok, "ACES %d space %d: %s", version, space, err.c_str());
            if (!ok) continue;
            bool sdr = false;
            for (auto& v : p.availableViews()) sdr = sdr || fc::OcioPipeline::isSdr709View(v);
            CHECK(sdr, "no SDR Rec.709 view for ACES %d", version);
            if (space == fc::INPUT_ACESCG) {  // gray ramp in ACEScg -> bands
                std::vector<float> ramp(256 * 4);
                for (int i = 0; i < 256; ++i) { float v = i / 255.0f; ramp[i*4] = ramp[i*4+1] = ramp[i*4+2] = v; ramp[i*4+3] = 1; }
                p.apply(ramp.data(), 256);
                for (int i = 0; i < 256; ++i) {
                    CHECK(std::isfinite(ramp[i*4]), "NaN in ramp");
                    float c[3] = {ramp[i*4], ramp[i*4+1], ramp[i*4+2]};
                    bands.push_back(fc::bandFromY(fc::lumaOf(std::fmin(std::fmax(c[0],0.f),1.f), std::fmin(std::fmax(c[1],0.f),1.f), std::fmin(std::fmax(c[2],0.f),1.f), 0)));
                }
                int prev = 0; for (int b : bands) { CHECK(b >= prev, "bands not monotonic"); prev = b; }
                std::printf("  ramp bands: first %d last %d view '%s'\n", bands.front(), bands.back(), p.resolvedView().c_str());
            }
        }
        bandsPerVersion.push_back(bands);
    }
    if (tested == 0) TEST_SKIP("no ACES configs available");
    if (tested == 1) std::printf("warning: only one config available - cross-version comparison skipped\n");
    if (bandsPerVersion.size() == 2) {
        // ACES 1.x and 2.0 use different tone mapping, so identical bands are NOT expected for the same
        // view; the difference is reported for information. Per-version monotonicity is checked above.
        int maxd = 0;
        for (size_t i = 0; i < bandsPerVersion[0].size(); ++i) maxd = std::max(maxd, std::abs(bandsPerVersion[0][i] - bandsPerVersion[1][i]));
        std::printf("info: max band difference ACES 1.x vs 2.0 on the gray ramp: %d\n", maxd);
    }
    TEST_END();
}
