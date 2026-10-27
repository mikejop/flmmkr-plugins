// falsecolor.metal - Metal kernel (enabled on macOS). All maths comes from fc_shared.h, which
// scripts/embed_metal.py inlines here (the kernel is compiled at runtime from a string).
// Compiled with fast-math OFF: band edges must stay exact.
#include <metal_stdlib>
using namespace metal;

#include "fc_shared.h"

kernel void falsecolor_kernel(device const float* src [[buffer(0)]],
                              device float* dst [[buffer(1)]],
                              constant FCParams& p [[buffer(2)]],
                              device const uchar4* overlay [[buffer(3)]],
                              uint2 gid [[thread_position_in_grid]]) {
    const int x = (int)gid.x, y = (int)gid.y;
    if (x >= p.w || y >= p.h) return;
    const int si = p.srcBase + y * p.srcRow + x * 4;
    FCPixel in;
    in.r = src[si]; in.g = src[si + 1]; in.b = src[si + 2]; in.a = src[si + 3];
    const FCMat3 m = p.mat;
    FCPixel o = fc_shade(in, p.source, m, p.matIdentity, p.mix);
    const int idx = fc_overlay_index(p.ovEnabled, p.ovX, p.ovY, p.ovW, p.ovH, p.frameX0, p.frameTop, p.winX0 + x, p.winY0 + y);
    if (idx >= 0) {
        const uchar4 t = overlay[idx];
        o = fc_blend_overlay(o, (float)t.x, (float)t.y, (float)t.z, (float)t.w);
    }
    const int di = p.dstBase + y * p.dstRow + x * 4;
    dst[di] = o.r; dst[di + 1] = o.g; dst[di + 2] = o.b; dst[di + 3] = o.a;
}
