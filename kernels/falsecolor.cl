// falsecolor.cl - OpenCL kernel. PREPARED, DISABLED (FALSECOLOR_GPU_OPENCL=OFF); not compiled or tested.
// Same maths as the CPU and Metal paths: everything comes from fc_shared.h.
#include "fc_shared.h"

__kernel void falsecolor_kernel(__global const float* src, __global float* dst, FCParams p,
                                __global const uchar4* overlay) {
    int x = get_global_id(0), y = get_global_id(1);
    if (x >= p.w || y >= p.h) return;
    int si = p.srcBase + y * p.srcRow + x * 4;
    FCPixel in; in.r = src[si]; in.g = src[si + 1]; in.b = src[si + 2]; in.a = src[si + 3];
    FCPixel o = fc_shade(in, p.source, p.mat, p.matIdentity, p.mix);
    int idx = fc_overlay_index(p.ovEnabled, p.ovX, p.ovY, p.ovW, p.ovH, p.frameX0, p.frameTop, p.winX0 + x, p.winY0 + y);
    if (idx >= 0) { uchar4 t = overlay[idx]; o = fc_blend_overlay(o, t.x, t.y, t.z, t.w); }
    int di = p.dstBase + y * p.dstRow + x * 4;
    dst[di] = o.r; dst[di + 1] = o.g; dst[di + 2] = o.b; dst[di + 3] = o.a;
}
