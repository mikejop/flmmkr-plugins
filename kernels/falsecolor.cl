// falsecolor.cl - OpenCL kernel. PREPARED, DISABLED by default (FALSECOLOR_GPU_OPENCL=OFF).
// Not compiled or tested on this phase. Same math as falseColorPixel(); no fast-math / native_* calls.
#include "palette_generated.h"

typedef struct {
    int w, h, srcBase, srcRow, convBase, convRow, dstBase, dstRow, lumaMode;
    float mix;
} FCArgs;

__constant float FC_PAL[15][3] = FC_PALETTE_INIT;
#define FC_BAND_EPS 1e-6f

__kernel void falsecolor_kernel(__global const float* src, __global const float* conv,
                                __global float* dst, FCArgs a) {
    int x = get_global_id(0), y = get_global_id(1);
    if (x >= a.w || y >= a.h) return;
    int si = a.srcBase + y * a.srcRow + x * 4;
    int ci = a.convBase + y * a.convRow + x * 4;
    int di = a.dstBase + y * a.dstRow + x * 4;
    float3 in = (float3)(src[si], src[si + 1], src[si + 2]);
    float ain = src[si + 3];
    float3 cv = (float3)(conv[ci], conv[ci + 1], conv[ci + 2]);
    float3 fcol = (float3)(0.0f);
    if (isfinite(cv.x) && isfinite(cv.y) && isfinite(cv.z)) {
        float3 c = clamp(cv, 0.0f, 1.0f);
        float yy = (a.lumaMode == 1) ? (0.2627f * c.x + 0.6780f * c.y + 0.0593f * c.z)
                                     : (0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z);
        yy = clamp(yy, 0.0f, 1.0f);
        int band = (int)floor(yy * 15.0f + FC_BAND_EPS);
        band = min(max(band, 0), 14);
        fcol = (float3)(FC_PAL[band][0], FC_PAL[band][1], FC_PAL[band][2]);
    }
    float3 inSafe = (float3)(isfinite(in.x) ? in.x : 0.0f, isfinite(in.y) ? in.y : 0.0f,
                             isfinite(in.z) ? in.z : 0.0f);
    float3 o = inSafe * (1.0f - a.mix) + fcol * a.mix;
    dst[di] = o.x; dst[di + 1] = o.y; dst[di + 2] = o.z;
    dst[di + 3] = isfinite(ain) ? ain : 1.0f;
}
