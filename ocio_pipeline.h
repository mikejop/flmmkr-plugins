// ocio_pipeline.h - the only module that depends on OpenColorIO (hidden behind pimpl).
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace fc {

enum InputSpace { INPUT_DISPLAY_709 = 0, INPUT_ACESCG = 1, INPUT_ACESCCT = 2, INPUT_ACES2065 = 3 };
enum AcesVersion { ACES_AUTO = 0, ACES_1X = 1, ACES_20 = 2 };

struct OcioSetup {
    std::string configPath;  // .ocio file (or ocio:// built-in URI)
    int inputSpace = INPUT_DISPLAY_709;
    int acesVersion = ACES_AUTO;
    std::string view;  // empty = first SDR Rec.709 view for the version

    bool operator==(const OcioSetup& o) const {
        return configPath == o.configPath && inputSpace == o.inputSpace &&
               acesVersion == o.acesVersion && view == o.view;
    }
};

class OcioPipeline {
public:
    OcioPipeline();
    ~OcioPipeline();

    // Rebuilds config/processor only when `s` differs from the last call. Returns false and
    // fills `err` when the ACES path cannot be built (caller falls back to Display Rec.709).
    bool configure(const OcioSetup& s, std::string& err);

    bool ready() const;  // true when an ACES processor is available
    // In-place batch conversion of contiguous RGBA float pixels. Thread-safe after configure().
    void apply(float* rgba, size_t pixelCount) const;

    std::string resolvedView() const;
    int resolvedAcesVersion() const;
    std::vector<std::string> availableViews() const;  // filtered by ACES version

    // Pure helpers (also used by the tests).
    static int detectAcesVersionFromNames(const std::string& configName,
                                          const std::string& description,
                                          const std::vector<std::string>& views);
    static bool viewMatchesVersion(const std::string& view, int version);
    static bool isSdr709View(const std::string& view);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fc
