// cpu_render.h - reference CPU renderer (multithreaded). No OFX/OCIO dependency:
// the colour-space conversion is injected as a batch callback.
#pragma once
#include <cstddef>
#include <functional>

namespace fc {

// Converts `pixelCount` contiguous RGBA float pixels in place (OCIO batch apply).
using ConvertFn = std::function<void(float* rgba, size_t pixelCount)>;

struct RenderParams {
    bool enable = true;
    float mix = 1.0f;
    int lumaMode = 0;  // LumaMode
};

// src/dst point at the first pixel of the window; rowFloats = row stride in floats.
// conv may be null (Display Rec.709: no conversion).
void cpuRender(const float* src, ptrdiff_t srcRowFloats, float* dst, ptrdiff_t dstRowFloats, int w,
               int h, const RenderParams& p, const ConvertFn* conv);

}  // namespace fc
