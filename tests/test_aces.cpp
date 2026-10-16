// ACEScct -> linear -> ACEScg -> display must equal OCIO's direct ACEScct -> display, with no NaN.
#include <cmath>
#include <vector>
#include "test_common.h"
#include "test_ocio_util.h"
int main() {
    std::string cfgPath = findConfig(1);
    if (cfgPath.empty()) cfgPath = findConfig(2);
    if (cfgPath.empty()) TEST_SKIP("no ACES config found (configs/ or built-in)");
    std::printf("config: %s\n", cfgPath.c_str());
    auto cfg = OCIO::Config::CreateFromFile(cfgPath.c_str());
    if (!cfg->hasRole("color_timing") || !cfg->hasRole("scene_linear")) TEST_SKIP("config lacks color_timing/scene_linear roles");
    const char* cct = cfg->getColorSpace("color_timing")->getName();
    const char* cg = cfg->getColorSpace("scene_linear")->getName();
    const char* display = cfg->getDefaultDisplay();

    fc::OcioSetup s; s.configPath = cfgPath; s.inputSpace = fc::INPUT_ACESCCT;
    fc::OcioPipeline pipe; std::string err;
    CHECK(pipe.configure(s, err), "pipeline configure: %s", err.c_str());
    std::string view = pipe.resolvedView();

    auto toLin = cfg->getProcessor(cct, cg)->getDefaultCPUProcessor();
    auto dvt = OCIO::DisplayViewTransform::Create();
    dvt->setSrc(cg); dvt->setDisplay(display); dvt->setView(view.c_str());
    auto fromCg = cfg->getProcessor(dvt)->getDefaultCPUProcessor();
    auto dvt2 = OCIO::DisplayViewTransform::Create();
    dvt2->setSrc(cct); dvt2->setDisplay(display); dvt2->setView(view.c_str());
    auto direct = cfg->getProcessor(dvt2)->getDefaultCPUProcessor();

    const int N = 257;
    std::vector<float> a(N * 4), b, c;
    for (int i = 0; i < N; ++i) { float v = i / (float)(N - 1); a[i*4] = v; a[i*4+1] = v * 0.5f + 0.1f; a[i*4+2] = 1.0f - v; a[i*4+3] = 1; }
    b = a; c = a;
    { OCIO::PackedImageDesc d(b.data(), N, 1, 4); toLin->apply(d); fromCg->apply(d); }
    { OCIO::PackedImageDesc d(c.data(), N, 1, 4); direct->apply(d); }
    pipe.apply(a.data(), N);  // plugin path
    float maxd = 0, maxp = 0;
    for (int i = 0; i < N * 4; ++i) {
        CHECK(std::isfinite(a[i]) && std::isfinite(b[i]) && std::isfinite(c[i]), "NaN/Inf at %d", i);
        maxd = std::fmax(maxd, std::fabs(b[i] - c[i]));
        maxp = std::fmax(maxp, std::fabs(a[i] - c[i]));
    }
    std::printf("max diff chained vs direct %.3g, plugin vs direct %.3g\n", maxd, maxp);
    CHECK(maxd < 1e-3f, "chained vs direct %.3g", maxd);
    CHECK(maxp < 1e-6f, "plugin vs direct %.3g", maxp);
    TEST_END();
}
