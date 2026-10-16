// falsecolor.metal - Metal Shading Language kernel (enabled on macOS).
// Same math as falseColorPixel() in falsecolor_core.cpp. Do NOT compile with fast-math:
// band comparisons must stay exact. The include below is inlined by scripts/embed_metal.py
// because the kernel is compiled at runtime from a string.
#include <metal_stdlib>
using namespace metal;

#include "palette_generated.h"

struct FCArgs {
    int w, h;
    int srcBase, srcRow;
    int convBase, convRow;
    int dstBase, dstRow;
    int lumaMode;
    float mix;
};

constant float FC_PAL[15][3] = FC_PALETTE_INIT;
constant float FC_BAND_EPS = 1e-6f;

kernel void falsecolor_kernel(device const float* src [[buffer(0)]],
                              device const float* conv [[buffer(1)]],
                              device float* dst [[buffer(2)]],
                              constant FCArgs& a [[buffer(3)]],
                              uint2 gid [[thread_position_in_grid]]) {
    if ((int)gid.x >= a.w || (int)gid.y >= a.h) return;
    const int si = a.srcBase + (int)gid.y * a.srcRow + (int)gid.x * 4;
    const int ci = a.convBase + (int)gid.y * a.convRow + (int)gid.x * 4;
    const int di = a.dstBase + (int)gid.y * a.dstRow + (int)gid.x * 4;

    const float3 in = float3(src[si], src[si + 1], src[si + 2]);
    const float ain = src[si + 3];
    const float3 cv = float3(conv[ci], conv[ci + 1], conv[ci + 2]);

    float3 fcol = float3(0.0f);  // NaN/Inf -> black
    if (isfinite(cv.x) && isfinite(cv.y) && isfinite(cv.z)) {
        const float3 c = clamp(cv, 0.0f, 1.0f);
        float y = (a.lumaMode == 1) ? (0.2627f * c.x + 0.6780f * c.y + 0.0593f * c.z)
                                    : (0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z);
        y = clamp(y, 0.0f, 1.0f);
        int band = (int)floor(y * 15.0f + FC_BAND_EPS);
        band = min(max(band, 0), 14);
        fcol = float3(FC_PAL[band][0], FC_PAL[band][1], FC_PAL[band][2]);
    }
    const float3 inSafe = float3(isfinite(in.x) ? in.x : 0.0f, isfinite(in.y) ? in.y : 0.0f,
                                 isfinite(in.z) ? in.z : 0.0f);
    const float3 o = inSafe * (1.0f - a.mix) + fcol * a.mix;
    dst[di] = o.x;
    dst[di + 1] = o.y;
    dst[di + 2] = o.z;
    dst[di + 3] = isfinite(ain) ? ain : 1.0f;
}
