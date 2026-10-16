// FalseColorPlugin.cpp - OFX 1.4 image effect "FalseColor".
//
// Uses the plain OFX C API (headers from the OpenFX repo: ofxCore.h, ofxImageEffect.h, ofxParam.h,
// ofxProperty.h, ofxGPURender.h) instead of the OFX Support Library C++ wrappers, because the
// support library has no Metal-buffer image handling. Sections that follow the Support Library /
// Resolve examples (descriptor, clips, render action, GPU properties) are marked [OFX].
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "ofxCore.h"
#include "ofxGPURender.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include "cpu_render.h"
#include "falsecolor_core.h"
#include "ocio_pipeline.h"
#include "plugin_log.h"
#include "render_path.h"
#if defined(__APPLE__)
#include "metal_backend.h"
#endif
// OpenCL / CUDA: prepared but disabled on this phase (kernels/falsecolor.cl / .cu).
// #ifdef FALSECOLOR_GPU_OPENCL ... #endif
// #ifdef FALSECOLOR_GPU_CUDA   ... #endif

#define kPluginId "com.flmmkr.FalseColor"

static OfxHost* gHost = nullptr;
static OfxPropertySuiteV1* gProp = nullptr;
static OfxImageEffectSuiteV1* gEffect = nullptr;
static OfxParameterSuiteV1* gParam = nullptr;

namespace {

// Parameter names
const char* kPEnable = "enable";
const char* kPMix = "mix";
const char* kPInput = "inputSpace";
const char* kPAces = "acesVersion";
const char* kPConfig = "ocioConfig";
const char* kPView = "outputView";
const char* kPLuma = "lumaWeights";
const char* kPPath = "renderPath";

struct Instance {
    OfxImageEffectHandle effect = nullptr;
    OfxImageClipHandle src = nullptr, dst = nullptr;
    OfxParamHandle pEnable = nullptr, pMix = nullptr, pInput = nullptr, pAces = nullptr,
                   pConfig = nullptr, pView = nullptr, pLuma = nullptr, pPath = nullptr;
    std::mutex mutex;
    fc::OcioPipeline ocio;
    bool ocioWarned = false;
    std::string lastErr;
    int lastBackend = -1;
#if defined(__APPLE__)
    std::unique_ptr<fc::MetalRunner> metal;
    void* metalQueue = nullptr;
#endif
};

Instance* getInstance(OfxImageEffectHandle h) {
    OfxPropertySetHandle ps;
    gEffect->getPropertySet(h, &ps);
    void* p = nullptr;
    gProp->propGetPointer(ps, kOfxPropInstanceData, 0, &p);
    return (Instance*)p;
}

int getInt(OfxParamHandle p, double t) { int v = 0; gParam->paramGetValueAtTime(p, t, &v); return v; }
double getDouble(OfxParamHandle p, double t) { double v = 0; gParam->paramGetValueAtTime(p, t, &v); return v; }
std::string getString(OfxParamHandle p, double t) {
    char* s = nullptr; gParam->paramGetValueAtTime(p, t, &s); return s ? s : "";
}

// ---------------------------------------------------------------------------------------------
// [OFX] Describe
// ---------------------------------------------------------------------------------------------
OfxStatus describe(OfxImageEffectHandle effect) {
    OfxPropertySetHandle p;
    gEffect->getPropertySet(effect, &p);
    gProp->propSetString(p, kOfxPropLabel, 0, "FLMMKR FALSE COLOR");
    gProp->propSetString(p, kOfxImageEffectPluginPropGrouping, 0, "FLMMKR");
    gProp->propSetString(p, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    gProp->propSetString(p, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
    gProp->propSetInt(p, kOfxImageEffectPropSupportsTiles, 0, 1);
    gProp->propSetInt(p, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
    gProp->propSetString(p, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderInstanceSafe);
#if defined(__APPLE__)
    // [OFX][GPU] Only Metal is advertised on macOS. OpenCL and CUDA are intentionally NOT declared.
    gProp->propSetString(p, kOfxImageEffectPropMetalRenderSupported, 0, "true");
#endif
    return kOfxStatOK;
}

void defineChoice(OfxParamSetHandle ps, const char* name, const char* label, int def,
                  std::initializer_list<const char*> opts, const char* hint) {
    OfxPropertySetHandle pp;
    gParam->paramDefine(ps, kOfxParamTypeChoice, name, &pp);
    gProp->propSetString(pp, kOfxPropLabel, 0, label);
    gProp->propSetString(pp, kOfxParamPropHint, 0, hint);
    int i = 0;
    for (auto o : opts) gProp->propSetString(pp, kOfxParamPropChoiceOption, i++, o);
    gProp->propSetInt(pp, kOfxParamPropDefault, 0, def);
}

// [OFX] DescribeInContext: clips + parameters
OfxStatus describeInContext(OfxImageEffectHandle effect) {
    OfxPropertySetHandle cp;
    gEffect->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &cp);
    gProp->propSetString(cp, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    gEffect->clipDefine(effect, kOfxImageEffectOutputClipName, &cp);
    gProp->propSetString(cp, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

    OfxParamSetHandle ps;
    gEffect->getParamSet(effect, &ps);
    OfxPropertySetHandle pp;

    gParam->paramDefine(ps, kOfxParamTypeBoolean, kPEnable, &pp);
    gProp->propSetString(pp, kOfxPropLabel, 0, "Enable");
    gProp->propSetInt(pp, kOfxParamPropDefault, 0, 1);

    gParam->paramDefine(ps, kOfxParamTypeDouble, kPMix, &pp);
    gProp->propSetString(pp, kOfxPropLabel, 0, "Mix");
    gProp->propSetDouble(pp, kOfxParamPropDefault, 0, 1.0);
    gProp->propSetDouble(pp, kOfxParamPropMin, 0, 0.0);
    gProp->propSetDouble(pp, kOfxParamPropMax, 0, 1.0);
    gProp->propSetDouble(pp, kOfxParamPropDisplayMin, 0, 0.0);
    gProp->propSetDouble(pp, kOfxParamPropDisplayMax, 0, 1.0);

    defineChoice(ps, kPInput, "Input Space", 0,
                 {"Display Rec.709 (pos-ODT)", "ACEScg (linear)", "ACEScct (log)", "ACES2065-1 (linear)"},
                 "Colour space of the incoming image. ACES options need an OCIO Config.");
    defineChoice(ps, kPAces, "ACES Version", 0, {"Auto (do config)", "ACES 1.x (legado)", "ACES 2.0"},
                 "Selects which Output Views are offered. Does not change the OCIO maths.");

    gParam->paramDefine(ps, kOfxParamTypeString, kPConfig, &pp);
    gProp->propSetString(pp, kOfxPropLabel, 0, "OCIO Config");
    gProp->propSetString(pp, kOfxParamPropStringMode, 0, kOfxParamStringIsFilePath);
    gProp->propSetString(pp, kOfxParamPropDefault, 0, "");
    gProp->propSetString(pp, kOfxParamPropHint, 0, "Path to the .ocio file (required for ACES input spaces).");

    // Output View is an editable string (dynamic choice lists are not portable across hosts).
    // The available views are printed in the log (~/Library/Logs/FalseColor.log).
    gParam->paramDefine(ps, kOfxParamTypeString, kPView, &pp);
    gProp->propSetString(pp, kOfxPropLabel, 0, "Output View");
    gProp->propSetString(pp, kOfxParamPropDefault, 0, "");
    gProp->propSetString(pp, kOfxParamPropHint, 0, "View name from the config. Empty = first SDR Rec.709 view for the ACES version.");

    defineChoice(ps, kPLuma, "Luma Weights", 0, {"Rec.709", "Rec.2020"}, "Weights used for Y.");
    defineChoice(ps, kPPath, "Render Path", 0, {"Automatico", "CPU", "GPU"},
                 "Automatico uses Metal when the host provides it.");
    return kOfxStatOK;
}

OfxStatus createInstance(OfxImageEffectHandle effect) {
    auto* inst = new Instance;
    inst->effect = effect;
    OfxParamSetHandle ps;
    gEffect->getParamSet(effect, &ps);
    auto get = [&](const char* n) { OfxParamHandle h; gParam->paramGetHandle(ps, n, &h, nullptr); return h; };
    inst->pEnable = get(kPEnable); inst->pMix = get(kPMix); inst->pInput = get(kPInput);
    inst->pAces = get(kPAces); inst->pConfig = get(kPConfig); inst->pView = get(kPView);
    inst->pLuma = get(kPLuma); inst->pPath = get(kPPath);
    gEffect->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &inst->src, nullptr);
    gEffect->clipGetHandle(effect, kOfxImageEffectOutputClipName, &inst->dst, nullptr);
    OfxPropertySetHandle ep;
    gEffect->getPropertySet(effect, &ep);
    gProp->propSetPointer(ep, kOfxPropInstanceData, 0, inst);
    return kOfxStatOK;
}

OfxStatus destroyInstance(OfxImageEffectHandle effect) {
    delete getInstance(effect);
    return kOfxStatOK;
}

// Re-reads colour parameters; (re)builds the OCIO processor only when something changed.
// Returns true if the ACES path is usable. Call with inst->mutex held.
bool configureOcio(Instance* inst, double t) {
    int space = getInt(inst->pInput, t);
    if (space == fc::INPUT_DISPLAY_709) return false;
    fc::OcioSetup s;
    s.inputSpace = space;
    s.acesVersion = getInt(inst->pAces, t);
    s.configPath = getString(inst->pConfig, t);
    s.view = getString(inst->pView, t);
    std::string err;
    bool ok = inst->ocio.configure(s, err);
    if (!ok && err != inst->lastErr) {
        fc::logLine("ACES path unavailable, using Display Rec.709: " + err);
    }
    inst->lastErr = ok ? "" : err;
    return ok;
}

// [OFX] IsIdentity: Enable off => copy input unchanged (host does the copy).
OfxStatus isIdentity(OfxImageEffectHandle effect, OfxPropertySetHandle in, OfxPropertySetHandle out) {
    Instance* inst = getInstance(effect);
    double t = 0;
    gProp->propGetDouble(in, kOfxPropTime, 0, &t);
    if (getInt(inst->pEnable, t) == 0) {
        gProp->propSetString(out, kOfxPropName, 0, kOfxImageEffectSimpleSourceClipName);
        gProp->propSetDouble(out, kOfxPropTime, 0, t);
        return kOfxStatReplyYes;
    }
    return kOfxStatReplyDefault;
}

// [OFX] InstanceChanged: log views when the config / version / input space changes.
OfxStatus instanceChanged(OfxImageEffectHandle effect, OfxPropertySetHandle in) {
    Instance* inst = getInstance(effect);
    char* name = nullptr;
    gProp->propGetString(in, kOfxPropName, 0, &name);
    if (!name) return kOfxStatReplyDefault;
    std::string n = name;
    if (n == kPConfig || n == kPAces || n == kPInput || n == kPView) {
        std::lock_guard<std::mutex> lock(inst->mutex);
        configureOcio(inst, 0);
    }
    return kOfxStatOK;
}

struct Img {
    OfxPropertySetHandle props = nullptr;
    void* data = nullptr;
    int bounds[4] = {0, 0, 0, 0};
    int rowBytes = 0;
};

bool fetchImage(OfxImageClipHandle clip, double t, Img& img) {
    if (gEffect->clipGetImage(clip, t, nullptr, &img.props) != kOfxStatOK || !img.props) return false;
    gProp->propGetPointer(img.props, kOfxImagePropData, 0, &img.data);
    for (int i = 0; i < 4; ++i) gProp->propGetInt(img.props, kOfxImagePropBounds, i, &img.bounds[i]);
    gProp->propGetInt(img.props, kOfxImagePropRowBytes, 0, &img.rowBytes);
    return img.data != nullptr;
}

// ---------------------------------------------------------------------------------------------
// [OFX] Render action
// ---------------------------------------------------------------------------------------------
OfxStatus render(OfxImageEffectHandle effect, OfxPropertySetHandle in) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    Instance* inst = getInstance(effect);
    std::lock_guard<std::mutex> lock(inst->mutex);

    double t = 0;
    gProp->propGetDouble(in, kOfxPropTime, 0, &t);
    int rw[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) gProp->propGetInt(in, kOfxImageEffectPropRenderWindow, i, &rw[i]);
    const int w = rw[2] - rw[0], h = rw[3] - rw[1];
    if (w <= 0 || h <= 0) return kOfxStatOK;

    int metalEnabled = 0;
    void* queue = nullptr;
#if defined(__APPLE__)
    if (gProp->propGetInt(in, kOfxImageEffectPropMetalEnabled, 0, &metalEnabled) != kOfxStatOK) metalEnabled = 0;
    if (metalEnabled) gProp->propGetPointer(in, kOfxImageEffectPropMetalCommandQueue, 0, &queue);
#endif
    const bool hostGpuBuffers = metalEnabled && queue;

    Img src, dst;
    if (!fetchImage(inst->src, t, src)) return kOfxStatFailed;
    if (!fetchImage(inst->dst, t, dst)) { gEffect->clipReleaseImage(src.props); return kOfxStatFailed; }

    fc::RenderParams rp;
    rp.enable = getInt(inst->pEnable, t) != 0;
    rp.mix = (float)getDouble(inst->pMix, t);
    rp.lumaMode = getInt(inst->pLuma, t);
    const bool aces = configureOcio(inst, t);
    fc::ConvertFn convFn = [inst](float* px, size_t n) { inst->ocio.apply(px, n); };
    const fc::ConvertFn* conv = aces ? &convFn : nullptr;

    const int srcRowF = src.rowBytes / 4, dstRowF = dst.rowBytes / 4;
    const int srcBase = (rw[1] - src.bounds[1]) * srcRowF + (rw[0] - src.bounds[0]) * 4;
    const int dstBase = (rw[1] - dst.bounds[1]) * dstRowF + (rw[0] - dst.bounds[0]) * 4;

    fc::PathChoice choice = (fc::PathChoice)getInt(inst->pPath, t);
    fc::Backend backend = fc::chooseBackend(choice, hostGpuBuffers);
    if (choice == fc::PathChoice::Gpu && !hostGpuBuffers && inst->lastBackend != -2) {
        fc::logLine("Render Path = GPU requested but the host did not provide Metal: using CPU");
        inst->lastBackend = -2;
    }

    std::string deviceName = "host CPU";
    std::string gpuNote;
    fc::RunResult result{fc::Backend::Cpu, false, ""};

#if defined(__APPLE__)
    if (hostGpuBuffers) {
        // Images are id<MTLBuffer>. The Metal runner uses the HOST's command queue (no own context).
        if (!inst->metal || inst->metalQueue != queue) {
            inst->metal.reset(new fc::MetalRunner(queue));
            inst->metalQueue = queue;
        }
        fc::MetalRunner& mr = *inst->metal;
        deviceName = mr.deviceName();

        auto gpuPath = [&](std::string& err) -> bool {
            if (!mr.ok()) { err = mr.lastError(); return false; }
            fc::MetalArgs a;
            a.w = w; a.h = h; a.lumaMode = rp.lumaMode; a.mix = rp.mix;
            a.srcBase = srcBase; a.srcRow = srcRowF; a.dstBase = dstBase; a.dstRow = dstRowF;
            a.convBase = srcBase; a.convRow = srcRowF;
            void* convBuf = src.data;
            if (rp.enable && aces) {
                // OCIO GPU shader generation is not wired up (see README "Nao implementado"):
                // the OCIO conversion runs on the CPU in a shared staging buffer, the band
                // mapping stays on the GPU.
                void* st = mr.stage(0, (size_t)w * h * 4);
                if (!st) { err = "staging buffer allocation failed"; return false; }
                if (!mr.blit(src.data, srcBase, srcRowF, 0, w, h, true)) { err = "blit failed"; return false; }
                inst->ocio.apply(mr.stagePtr(0), (size_t)w * h);
                convBuf = st; a.convBase = 0; a.convRow = w * 4;
                gpuNote = " (OCIO conversion on CPU)";
            }
            if (!rp.enable) {  // plain copy through the same kernel with mix 0
                a.mix = 0.0f;
            }
            if (!mr.run(src.data, convBuf, dst.data, a)) { err = mr.lastError(); return false; }
            return true;
        };
        auto cpuViaStaging = [&] {
            // CPU maths on host GPU buffers: blit to shared staging, run the reference renderer, blit back.
            size_t floats = (size_t)w * h * 4;
            if (!mr.stage(0, floats) || !mr.stage(1, floats)) return;
            mr.blit(src.data, srcBase, srcRowF, 0, w, h, true);
            fc::cpuRender(mr.stagePtr(0), w * 4, mr.stagePtr(1), w * 4, w, h, rp, conv);
            mr.blit(dst.data, dstBase, dstRowF, 1, w, h, false);
        };
        result = fc::runWithFallback(backend, gpuPath, cpuViaStaging);
    } else
#endif
    {
        const float* sp = (const float*)((const char*)src.data) + srcBase;
        float* dp = (float*)((char*)dst.data) + dstBase;
        fc::cpuRender(sp, srcRowF, dp, dstRowF, w, h, rp, conv);
    }

    if (result.fellBack) fc::logLine("GPU failed for this frame, CPU fallback used: " + result.error);
    int bk = result.used == fc::Backend::Metal ? 1 : 0;
    double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    if (bk != inst->lastBackend || fc::verboseLog()) {
        inst->lastBackend = bk;
        fc::logLine(std::string("render backend: ") + (bk ? "Metal" : "CPU") + gpuNote + ", device: " + deviceName +
                    ", " + std::to_string(ms) + " ms/frame (" + std::to_string(w) + "x" + std::to_string(h) + ")");
    }

    gEffect->clipReleaseImage(src.props);
    gEffect->clipReleaseImage(dst.props);
    return kOfxStatOK;
}

OfxStatus pluginMain(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle out) {
    try {
        OfxImageEffectHandle effect = (OfxImageEffectHandle)handle;
        if (!std::strcmp(action, kOfxActionLoad)) {
            if (!gHost) return kOfxStatErrMissingHostFeature;
            gProp = (OfxPropertySuiteV1*)gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1);
            gEffect = (OfxImageEffectSuiteV1*)gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1);
            gParam = (OfxParameterSuiteV1*)gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1);
            return (gProp && gEffect && gParam) ? kOfxStatOK : kOfxStatErrMissingHostFeature;
        }
        if (!std::strcmp(action, kOfxActionUnload)) return kOfxStatOK;
        if (!std::strcmp(action, kOfxActionDescribe)) return describe(effect);
        if (!std::strcmp(action, kOfxImageEffectActionDescribeInContext)) return describeInContext(effect);
        if (!std::strcmp(action, kOfxActionCreateInstance)) return createInstance(effect);
        if (!std::strcmp(action, kOfxActionDestroyInstance)) return destroyInstance(effect);
        if (!std::strcmp(action, kOfxImageEffectActionIsIdentity)) return isIdentity(effect, in, out);
        if (!std::strcmp(action, kOfxActionInstanceChanged)) return instanceChanged(effect, in);
        if (!std::strcmp(action, kOfxImageEffectActionRender)) return render(effect, in);
        // RoD defaults to the input RoD and RoI to the output RoI (point operator): host defaults apply.
    } catch (const std::exception& e) {
        fc::logLine(std::string("exception in action ") + action + ": " + e.what());
        return kOfxStatFailed;
    } catch (...) {
        return kOfxStatFailed;
    }
    return kOfxStatReplyDefault;
}

void setHost(OfxHost* h) { gHost = h; }

OfxPlugin gPlugin = {kOfxImageEffectPluginApi, 1, kPluginId, 1, 0, setHost, pluginMain};

}  // namespace

OfxExport int OfxGetNumberOfPlugins(void) { return 1; }
OfxExport OfxPlugin* OfxGetPlugin(int nth) { return nth == 0 ? &gPlugin : nullptr; }
