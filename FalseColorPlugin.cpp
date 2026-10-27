// FalseColorPlugin.cpp - OFX 1.4 image effect "FLMMKR FALSE COLOR".
//
// Uses the plain OFX C API (headers from the OpenFX repo: ofxCore.h, ofxImageEffect.h, ofxParam.h,
// ofxProperty.h, ofxGPURender.h) instead of the OFX Support Library C++ wrappers, because the
// support library has no Metal-buffer image handling. Sections that follow the OFX / Resolve
// examples (descriptor, clips, render action, GPU properties) are marked [OFX].
//
// Generic OpenFX: Filter and General contexts, RGB/RGBA, 8-bit, 16-bit and float, on macOS, Windows
// and Linux. Rendering always goes to the GPU (Metal) when the host provides it. The CPU renderer is only the
// safety net: hosts without Metal, or a GPU error on a frame (that frame is redone on the CPU).
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
#include "overlay.h"
#include "plugin_log.h"
#include "render_path.h"
#if defined(__APPLE__)
#include "metal_backend.h"
#endif
// OpenCL / CUDA: prepared but disabled on this phase (kernels/falsecolor.cl / .cu).

#define kPluginId "com.flmmkr.FalseColor"

static OfxHost* gHost = nullptr;
static OfxPropertySuiteV1* gProp = nullptr;
static OfxImageEffectSuiteV1* gEffect = nullptr;
static OfxParameterSuiteV1* gParam = nullptr;

namespace {

// Parameter names (stable ids; labels are what the user sees).
const char* kPEnable = "enable";
const char* kPSource = "source";
const char* kPMix = "mix";
const char* kPScale = "showScale";
const char* kPLabels = "showLabels";

struct Instance {
    OfxImageEffectHandle effect = nullptr;
    OfxImageClipHandle src = nullptr, dst = nullptr;
    OfxParamHandle pEnable = nullptr, pSource = nullptr, pMix = nullptr, pScale = nullptr, pLabels = nullptr;
    std::mutex mutex;

    // Scale overlay cache: rebuilt only when frame size or the label switch changes.
    fc::ScaleOverlay overlay;
    int ovFrameW = -1, ovFrameH = -1, ovLabels = -1;
    unsigned ovGeneration = 0;

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

// ---------------------------------------------------------------------------------------------
// [OFX] Describe
// ---------------------------------------------------------------------------------------------
OfxStatus describe(OfxImageEffectHandle effect) {
    OfxPropertySetHandle p;
    gEffect->getPropertySet(effect, &p);
    gProp->propSetString(p, kOfxPropLabel, 0, "FLMMKR FALSE COLOR");
    gProp->propSetString(p, kOfxImageEffectPluginPropGrouping, 0, "FLMMKR");
    // Generic OFX: any host that loads OpenFX image effects, on macOS, Windows and Linux.
    gProp->propSetString(p, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    gProp->propSetString(p, kOfxImageEffectPropSupportedContexts, 1, kOfxImageEffectContextGeneral);
    gProp->propSetString(p, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
    gProp->propSetString(p, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort);
    gProp->propSetString(p, kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthByte);
    gProp->propSetInt(p, kOfxImageEffectPropSupportsTiles, 0, 1);
    gProp->propSetInt(p, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
    gProp->propSetString(p, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderInstanceSafe);
#if defined(__APPLE__)
    // [OFX][GPU] Only Metal is advertised on macOS. OpenCL and CUDA are intentionally NOT declared.
    gProp->propSetString(p, kOfxImageEffectPropMetalRenderSupported, 0, "true");
#endif
    return kOfxStatOK;
}

OfxPropertySetHandle defineBool(OfxParamSetHandle ps, const char* name, const char* label, int def, const char* hint) {
    OfxPropertySetHandle pp;
    gParam->paramDefine(ps, kOfxParamTypeBoolean, name, &pp);
    gProp->propSetString(pp, kOfxPropLabel, 0, label);
    gProp->propSetString(pp, kOfxParamPropHint, 0, hint);
    gProp->propSetInt(pp, kOfxParamPropDefault, 0, def);
    return pp;
}

// [OFX] DescribeInContext (Filter and General use the same two clips): clips + parameters
OfxStatus describeInContext(OfxImageEffectHandle effect) {
    OfxPropertySetHandle cp;
    for (const char* clip : {kOfxImageEffectSimpleSourceClipName, kOfxImageEffectOutputClipName}) {
        gEffect->clipDefine(effect, clip, &cp);
        gProp->propSetString(cp, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
        gProp->propSetString(cp, kOfxImageEffectPropSupportedComponents, 1, kOfxImageComponentRGB);
        gProp->propSetInt(cp, kOfxImageEffectPropSupportsTiles, 0, 1);
    }

    OfxParamSetHandle ps;
    gEffect->getParamSet(effect, &ps);
    OfxPropertySetHandle pp;

    defineBool(ps, kPEnable, "Enable", 1, "Desligado: a imagem passa sem alteracao.");

    gParam->paramDefine(ps, kOfxParamTypeChoice, kPSource, &pp);
    gProp->propSetString(pp, kOfxPropLabel, 0, "Source");
    gProp->propSetString(pp, kOfxParamPropHint, 0,
                         "Espaco de cor / curva da imagem que chega no no. O false color decodifica essa curva "
                         "e mede a luminancia como um sinal Rec.709.");
    for (int i = 0; i < FC_NUM_SOURCES; ++i) gProp->propSetString(pp, kOfxParamPropChoiceOption, i, fc::kSourceNames[i]);
    gProp->propSetInt(pp, kOfxParamPropDefault, 0, FC_SRC_REC709);

    gParam->paramDefine(ps, kOfxParamTypeDouble, kPMix, &pp);
    gProp->propSetString(pp, kOfxPropLabel, 0, "Mix");
    gProp->propSetDouble(pp, kOfxParamPropDefault, 0, 1.0);
    gProp->propSetDouble(pp, kOfxParamPropMin, 0, 0.0);
    gProp->propSetDouble(pp, kOfxParamPropMax, 0, 1.0);
    gProp->propSetDouble(pp, kOfxParamPropDisplayMin, 0, 0.0);
    gProp->propSetDouble(pp, kOfxParamPropDisplayMax, 0, 1.0);

    defineBool(ps, kPScale, "Escala", 1, "Mostra a escala do Sistema de Zonas no lado esquerdo da imagem.");
    defineBool(ps, kPLabels, "Legendas", 1, "Mostra o significado de cada cor na escala.");
    return kOfxStatOK;
}

OfxStatus createInstance(OfxImageEffectHandle effect) {
    auto* inst = new Instance;
    inst->effect = effect;
    OfxParamSetHandle ps;
    gEffect->getParamSet(effect, &ps);
    auto get = [&](const char* n) { OfxParamHandle h = nullptr; gParam->paramGetHandle(ps, n, &h, nullptr); return h; };
    inst->pEnable = get(kPEnable); inst->pSource = get(kPSource); inst->pMix = get(kPMix);
    inst->pScale = get(kPScale); inst->pLabels = get(kPLabels);
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

struct Img {
    OfxPropertySetHandle props = nullptr;
    void* data = nullptr;
    int bounds[4] = {0, 0, 0, 0};
    int rowBytes = 0;
    int components = 4;        // 3 = RGB, 4 = RGBA
    int bytesPerComponent = 4; // 1 = 8-bit, 2 = 16-bit, 4 = float
};

bool fetchImage(OfxImageClipHandle clip, double t, Img& img) {
    if (gEffect->clipGetImage(clip, t, nullptr, &img.props) != kOfxStatOK || !img.props) return false;
    gProp->propGetPointer(img.props, kOfxImagePropData, 0, &img.data);
    for (int i = 0; i < 4; ++i) gProp->propGetInt(img.props, kOfxImagePropBounds, i, &img.bounds[i]);
    gProp->propGetInt(img.props, kOfxImagePropRowBytes, 0, &img.rowBytes);
    char* depth = nullptr;
    char* comps = nullptr;
    gProp->propGetString(img.props, kOfxImageEffectPropPixelDepth, 0, &depth);
    gProp->propGetString(img.props, kOfxImageEffectPropComponents, 0, &comps);
    if (depth && !std::strcmp(depth, kOfxBitDepthByte)) img.bytesPerComponent = 1;
    else if (depth && !std::strcmp(depth, kOfxBitDepthShort)) img.bytesPerComponent = 2;
    else if (depth && !std::strcmp(depth, kOfxBitDepthFloat)) img.bytesPerComponent = 4;
    else img.bytesPerComponent = 0;
    if (comps && !std::strcmp(comps, kOfxImageComponentRGB)) img.components = 3;
    else if (comps && !std::strcmp(comps, kOfxImageComponentRGBA)) img.components = 4;
    else img.components = 0;
    return img.data != nullptr;
}

// Frame rectangle in pixel coordinates (x1, y1, x2, y2), from the source RoD and the render scale.
void frameRect(Instance* inst, double t, OfxPropertySetHandle in, const Img& fallback, int r[4]) {
    OfxRectD rod{0, 0, 0, 0};
    double rs[2] = {1.0, 1.0}, par = 1.0;
    gProp->propGetDouble(in, kOfxImageEffectPropRenderScale, 0, &rs[0]);
    gProp->propGetDouble(in, kOfxImageEffectPropRenderScale, 1, &rs[1]);
    OfxPropertySetHandle cp = nullptr;
    if (gEffect->clipGetPropertySet(inst->src, &cp) == kOfxStatOK && cp)
        gProp->propGetDouble(cp, kOfxImagePropPixelAspectRatio, 0, &par);
    if (par <= 0) par = 1.0;
    if (gEffect->clipGetRegionOfDefinition(inst->src, t, &rod) == kOfxStatOK && rod.x2 > rod.x1 && rod.y2 > rod.y1) {
        r[0] = (int)std::floor(rod.x1 * rs[0] / par);
        r[1] = (int)std::floor(rod.y1 * rs[1]);
        r[2] = (int)std::ceil(rod.x2 * rs[0] / par);
        r[3] = (int)std::ceil(rod.y2 * rs[1]);
    } else {
        for (int i = 0; i < 4; ++i) r[i] = fallback.bounds[i];
    }
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
    if (!src.components || !src.bytesPerComponent || src.components != dst.components ||
        src.bytesPerComponent != dst.bytesPerComponent) {
        fc::logLine("formato de imagem nao suportado (use RGB/RGBA de 8, 16 ou 32 bits)");
        gEffect->clipReleaseImage(src.props);
        gEffect->clipReleaseImage(dst.props);
        return kOfxStatErrImageFormat;
    }
    const bool floatRGBA = src.components == 4 && src.bytesPerComponent == 4;

    // ---- parameters -> FCParams (identical for CPU and GPU) -----------------------------------
    FCParams p{};
    p.w = w; p.h = h;
    p.srcRow = src.rowBytes / 4;
    p.dstRow = dst.rowBytes / 4;
    p.srcBase = (rw[1] - src.bounds[1]) * p.srcRow + (rw[0] - src.bounds[0]) * 4;
    p.dstBase = (rw[1] - dst.bounds[1]) * p.dstRow + (rw[0] - dst.bounds[0]) * 4;
    p.winX0 = rw[0]; p.winY0 = rw[1];
    p.source = getInt(inst->pSource, t);
    if (p.source < 0 || p.source >= FC_NUM_SOURCES) p.source = FC_SRC_REC709;
    p.mix = (float)getDouble(inst->pMix, t);
    bool ident = true;
    p.mat = fc::sourceToRec709(p.source, &ident);
    p.matIdentity = ident ? 1 : 0;

    int fr[4];
    frameRect(inst, t, in, src, fr);
    p.frameX0 = fr[0]; p.frameTop = fr[3];
    const bool showScale = getInt(inst->pScale, t) != 0;
    const int labels = getInt(inst->pLabels, t) != 0 ? 1 : 0;
    if (showScale) {
        const int fw = fr[2] - fr[0], fh = fr[3] - fr[1];
        if (fw != inst->ovFrameW || fh != inst->ovFrameH || labels != inst->ovLabels) {
            inst->overlay = fc::buildScaleOverlay(fw, fh, labels != 0);
            inst->ovFrameW = fw; inst->ovFrameH = fh; inst->ovLabels = labels;
            ++inst->ovGeneration;
        }
    }
    const bool ov = showScale && inst->overlay.valid();
    p.ovEnabled = ov ? 1 : 0;
    if (ov) { p.ovX = inst->overlay.x; p.ovY = inst->overlay.y; p.ovW = inst->overlay.w; p.ovH = inst->overlay.h; }
    const uint8_t* ovData = ov ? inst->overlay.rgba.data() : nullptr;

    std::string deviceName = "host CPU";
    fc::RunResult result{fc::Backend::Cpu, false, ""};

#if defined(__APPLE__)
    if (hostGpuBuffers && floatRGBA) {
        // Images are id<MTLBuffer>. The Metal runner uses the HOST's command queue (no own context).
        if (!inst->metal || inst->metalQueue != queue) {
            inst->metal.reset(new fc::MetalRunner(queue));
            inst->metalQueue = queue;
            inst->lastBackend = -1;
        }
        fc::MetalRunner& mr = *inst->metal;
        deviceName = mr.deviceName();
        auto gpuPath = [&](std::string& err) -> bool {
            if (!mr.ok()) { err = mr.lastError(); return false; }
            if (ov && !mr.setOverlay(ovData, inst->overlay.rgba.size(), inst->ovGeneration)) { err = "overlay upload failed"; return false; }
            if (!mr.run(src.data, dst.data, p)) { err = mr.lastError(); return false; }
            return true;
        };
        auto cpuViaStaging = [&] {
            // Same maths on the CPU over the host's GPU buffers: blit in, render, blit back.
            const size_t floats = (size_t)w * h * 4;
            if (!mr.stage(0, floats) || !mr.stage(1, floats)) return;
            mr.blit(src.data, p.srcBase, p.srcRow, 0, w, h, true);
            FCParams q = p;
            q.srcBase = q.dstBase = 0;
            q.srcRow = q.dstRow = w * 4;
            fc::cpuRender(mr.stagePtr(0), mr.stagePtr(1), q, ovData);
            mr.blit(dst.data, p.dstBase, p.dstRow, 1, w, h, false);
        };
        result = fc::runWithFallback(fc::Backend::Metal, gpuPath, cpuViaStaging);
    } else
#endif
    if (floatRGBA) {
        fc::cpuRender((const float*)src.data, (float*)dst.data, p, ovData);
    } else {  // 8/16-bit or RGB images from hosts other than Resolve
        const int pix = src.components * src.bytesPerComponent;
        const uint8_t* sw = (const uint8_t*)src.data + (ptrdiff_t)(rw[1] - src.bounds[1]) * src.rowBytes + (ptrdiff_t)(rw[0] - src.bounds[0]) * pix;
        uint8_t* dw = (uint8_t*)dst.data + (ptrdiff_t)(rw[1] - dst.bounds[1]) * dst.rowBytes + (ptrdiff_t)(rw[0] - dst.bounds[0]) * pix;
        fc::cpuRenderAny(sw, src.rowBytes, dw, dst.rowBytes, src.components, src.bytesPerComponent, p, ovData);
    }

    if (result.fellBack) fc::logLine("GPU falhou neste frame, refeito na CPU: " + result.error);
    const int bk = result.used == fc::Backend::Metal ? 1 : 0;
    const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    if (bk != inst->lastBackend || fc::verboseLog()) {
        inst->lastBackend = bk;
        fc::logLine(std::string("render backend: ") + (bk ? "Metal" : "CPU") + ", device: " + deviceName +
                    ", source: " + fc::kSourceNames[p.source] + ", " + std::to_string(ms) + " ms/frame (" +
                    std::to_string(w) + "x" + std::to_string(h) + ")");
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
        if (!std::strcmp(action, kOfxImageEffectActionRender)) return render(effect, in);
        // RoD = input RoD and RoI = output RoI (point operator + overlay drawn inside the frame):
        // the host defaults apply.
    } catch (const std::exception& e) {
        fc::logLine(std::string("exception in action ") + action + ": " + e.what());
        return kOfxStatFailed;
    } catch (...) {
        return kOfxStatFailed;
    }
    return kOfxStatReplyDefault;
}

void setHost(OfxHost* h) { gHost = h; }

OfxPlugin gPlugin = {kOfxImageEffectPluginApi, 1, kPluginId, 1, 1, setHost, pluginMain};

}  // namespace

OfxExport int OfxGetNumberOfPlugins(void) { return 1; }
OfxExport OfxPlugin* OfxGetPlugin(int nth) { return nth == 0 ? &gPlugin : nullptr; }
