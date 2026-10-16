#include "ocio_pipeline.h"

#include <OpenColorIO/OpenColorIO.h>

#include <algorithm>
#include <cctype>
#include <exception>

#include "plugin_log.h"

namespace OCIO = OCIO_NAMESPACE;

namespace fc {

struct OcioPipeline::Impl {
    bool haveSetup = false;
    OcioSetup setup;
    OCIO::ConstConfigRcPtr config;
    OCIO::ConstCPUProcessorRcPtr cpu;
    std::string view;
    int version = ACES_1X;
    std::vector<std::string> views;
};

OcioPipeline::OcioPipeline() : impl_(new Impl) {}
OcioPipeline::~OcioPipeline() = default;

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// UNVERIFIED heuristics (names differ between config releases): see README "A verificar".
bool OcioPipeline::viewMatchesVersion(const std::string& view, int version) {
    std::string v = lower(view);
    bool is2 = v.find("2.0") != std::string::npos;
    bool is1 = v.find("1.0") != std::string::npos || v.find("1.x") != std::string::npos ||
               v.find("1.3") != std::string::npos || v.find("1.2") != std::string::npos;
    if (version == ACES_20) return is2;
    if (version == ACES_1X) return is1 || !is2;
    return true;
}

bool OcioPipeline::isSdr709View(const std::string& view) {
    std::string v = lower(view);
    return v.find("sdr") != std::string::npos &&
           (v.find("709") != std::string::npos || v.find("video") != std::string::npos);
}

int OcioPipeline::detectAcesVersionFromNames(const std::string& configName,
                                             const std::string& description,
                                             const std::vector<std::string>& views) {
    std::string t = lower(configName + " " + description);
    if (t.find("aces-v2") != std::string::npos || t.find("aces v2") != std::string::npos ||
        t.find("aces 2.0") != std::string::npos)
        return ACES_20;
    if (t.find("aces-v1") != std::string::npos || t.find("aces v1") != std::string::npos ||
        t.find("aces 1.") != std::string::npos)
        return ACES_1X;
    for (auto& v : views)
        if (lower(v).find("2.0") != std::string::npos) return ACES_20;
    return ACES_AUTO;  // undetermined
}

static const char* roleFor(int space) {
    switch (space) {
        case INPUT_ACESCG: return "scene_linear";       // UNVERIFIED role mapping
        case INPUT_ACESCCT: return "color_timing";      // UNVERIFIED role mapping
        case INPUT_ACES2065: return "aces_interchange"; // UNVERIFIED role mapping
        default: return nullptr;
    }
}

bool OcioPipeline::configure(const OcioSetup& s, std::string& err) {
    Impl& d = *impl_;
    if (d.haveSetup && d.setup == s) return d.cpu != nullptr;
    d.haveSetup = true;
    d.setup = s;
    d.cpu.reset();  // never reuse a processor from another config/params
    d.config.reset();
    d.views.clear();
    d.view.clear();
    if (s.inputSpace == INPUT_DISPLAY_709) return false;  // nothing to build

    try {
        if (s.configPath.empty()) {
            err = "OCIO Config is empty: using Display Rec.709";
            return false;
        }
        d.config = OCIO::Config::CreateFromFile(s.configPath.c_str());

        // Roles
        std::string roles;
        for (int i = 0; i < d.config->getNumRoles(); ++i) {
            roles += d.config->getRoleName(i);
            roles += i + 1 < d.config->getNumRoles() ? ", " : "";
        }
        logLine("OCIO roles in config: " + roles);
        const char* role = roleFor(s.inputSpace);
        if (!d.config->hasRole(role)) {
            err = std::string("config has no role '") + role + "'";
            return false;
        }
        OCIO::ConstColorSpaceRcPtr srcCs = d.config->getColorSpace(role);
        if (!srcCs) {
            err = std::string("role '") + role + "' does not resolve to a color space";
            return false;
        }

        const char* display = d.config->getDefaultDisplay();
        std::vector<std::string> all;
        for (int i = 0; i < d.config->getNumViews(display); ++i)
            all.push_back(d.config->getView(display, i));

        int ver = s.acesVersion;
        if (ver == ACES_AUTO) {
            ver = detectAcesVersionFromNames(d.config->getName() ? d.config->getName() : "",
                                             d.config->getDescription() ? d.config->getDescription() : "",
                                             all);
            if (ver == ACES_AUTO) {
                logLine("ACES version could not be detected from the config: assuming ACES 1.x");
                ver = ACES_1X;
            }
        }
        d.version = ver;
        for (auto& v : all)
            if (viewMatchesVersion(v, ver)) d.views.push_back(v);
        if (d.views.empty()) {
            logLine("no views match the selected ACES version: listing all views");
            d.views = all;
        }
        std::string list;
        for (auto& v : d.views) list += "'" + v + "' ";
        logLine("available views (display '" + std::string(display) + "'): " + list);

        std::string view = s.view;
        if (view.empty()) {
            for (auto& v : d.views)
                if (isSdr709View(v)) { view = v; break; }
            if (view.empty() && !d.views.empty()) view = d.views.front();
        } else if (std::find(all.begin(), all.end(), view) == all.end()) {
            err = "view '" + view + "' does not exist in the config (see log for the list)";
            return false;
        }
        if (view.empty()) { err = "config has no views"; return false; }
        d.view = view;

        auto dvt = OCIO::DisplayViewTransform::Create();
        dvt->setSrc(srcCs->getName());
        dvt->setDisplay(display);
        dvt->setView(view.c_str());
        dvt->setDirection(OCIO::TRANSFORM_DIR_FORWARD);
        OCIO::ConstProcessorRcPtr proc = d.config->getProcessor(dvt);
        d.cpu = proc->getDefaultCPUProcessor();
        logLine("OCIO processor built: " + std::string(srcCs->getName()) + " -> " + display + " / " + view);
        return true;
    } catch (const std::exception& e) {
        err = std::string("OCIO error: ") + e.what();
        d.cpu.reset();
        return false;
    }
}

bool OcioPipeline::ready() const { return impl_->cpu != nullptr; }

void OcioPipeline::apply(float* rgba, size_t pixelCount) const {
    if (!impl_->cpu || pixelCount == 0) return;
    // Large batches are split only to keep `long` widths sane.
    const size_t kMax = 1u << 24;
    for (size_t off = 0; off < pixelCount; off += kMax) {
        size_t n = std::min(kMax, pixelCount - off);
        OCIO::PackedImageDesc desc(rgba + off * 4, (long)n, 1, 4);
        impl_->cpu->apply(desc);
    }
}

std::string OcioPipeline::resolvedView() const { return impl_->view; }
int OcioPipeline::resolvedAcesVersion() const { return impl_->version; }
std::vector<std::string> OcioPipeline::availableViews() const { return impl_->views; }

}  // namespace fc
