// falsecolor.cu - CUDA kernel. PREPARED, DISABLED by default (FALSECOLOR_GPU_CUDA=OFF).
// For Windows/Linux only. Not compiled or tested on this phase. Compile WITHOUT --use_fast_math.
#include <math.h>
#include "palette_generated.h"

struct FCArgs {
    int w, h, srcBase, srcRow, convBase, convRow, dstBase, dstRow, lumaMode;
    float mix;
};

__constant__ float FC_PAL[15][3] = FC_PALETTE_INIT;
#define FC_BAND_EPS 1e-6f

__global__ void falsecolor_kernel(const float* src, const float* conv, float* dst, FCArgs a) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= a.w || y >= a.h) return;
    int si = a.srcBase + y * a.srcRow + x * 4;
    int ci = a.convBase + y * a.convRow + x * 4;
    int di = a.dstBase + y * a.dstRow + x * 4;
    float r = src[si], g = src[si + 1], b = src[si + 2], ain = src[si + 3];
    float cr = conv[ci], cg = conv[ci + 1], cb = conv[ci + 2];
    float fr = 0.f, fg = 0.f, fb = 0.f;
    if (isfinite(cr) && isfinite(cg) && isfinite(cb)) {
        cr = fminf(fmaxf(cr, 0.f), 1.f); cg = fminf(fmaxf(cg, 0.f), 1.f); cb = fminf(fmaxf(cb, 0.f), 1.f);
        float yy = (a.lumaMode == 1) ? (0.2627f * cr + 0.6780f * cg + 0.0593f * cb)
                                     : (0.2126f * cr + 0.7152f * cg + 0.0722f * cb);
        yy = fminf(fmaxf(yy, 0.f), 1.f);
        int band = (int)floorf(yy * 15.0f + FC_BAND_EPS);
        band = min(max(band, 0), 14);
        fr = FC_PAL[band][0]; fg = FC_PAL[band][1]; fb = FC_PAL[band][2];
    }
    if (!isfinite(r)) r = 0.f;
    if (!isfinite(g)) g = 0.f;
    if (!isfinite(b)) b = 0.f;
    dst[di] = r * (1.f - a.mix) + fr * a.mix;
    dst[di + 1] = g * (1.f - a.mix) + fg * a.mix;
    dst[di + 2] = b * (1.f - a.mix) + fb * a.mix;
    dst[di + 3] = isfinite(ain) ? ain : 1.f;
}
