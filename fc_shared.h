// fc_shared.h - THE single definition of the false-color math.
// Compiled as C++ (CPU reference), as Metal Shading Language (inlined into the kernel source by
// scripts/embed_metal.py) and, prepared, as OpenCL C / CUDA. Keep it to plain C-style code.
//
// Pipeline per pixel:
//   1. decode the signal with the Source curve (log/gamma -> linear)
//   2. source primaries -> Rec.709 primaries (3x3 matrix, computed on the CPU)
//   3. clamp to [0,1]                              (the only colour clamp)
//   4. re-encode as a Rec.709 video signal: BT.709 OETF for scene-referred (log) sources,
//      gamma 2.4 (BT.1886) for display-referred sources; Rec.709 itself is passed through
//   5. Y = Rec.709 luma of that signal, band = min(floor(Y*15), 14)
//   6. palette colour, mix with the input, alpha from the input; NaN/Inf -> black
#ifndef FC_SHARED_H
#define FC_SHARED_H

#ifndef FC_GENERATOR
#include "palette_generated.h"
#else
#define FC_PALETTE_INIT {{0}}  /* tools/gen_palette builds the real header */
#endif

#if defined(__METAL_VERSION__)
#define FC_INLINE static inline
#define FC_CONST constant
#define FC_LOG2(x) metal::precise::log2(x)
#define FC_EXP2(x) metal::precise::exp2(x)
#define FC_POW(x, y) metal::precise::pow(x, y)
#define FC_FLOOR(x) metal::floor(x)
#define FC_ISFINITE(x) metal::isfinite(x)
#elif defined(__OPENCL_VERSION__)  // prepared, not compiled on this phase
#define FC_INLINE static inline
#define FC_CONST __constant
#define FC_LOG2(x) log2(x)
#define FC_EXP2(x) exp2(x)
#define FC_POW(x, y) pow(x, y)
#define FC_FLOOR(x) floor(x)
#define FC_ISFINITE(x) isfinite(x)
#elif defined(__CUDACC__)  // prepared, not compiled on this phase
#define FC_INLINE static __device__ __host__ inline
#define FC_CONST __constant__
#define FC_LOG2(x) log2f(x)
#define FC_EXP2(x) exp2f(x)
#define FC_POW(x, y) powf(x, y)
#define FC_FLOOR(x) floorf(x)
#define FC_ISFINITE(x) isfinite(x)
#else
#include <cmath>
#define FC_INLINE static inline
#define FC_CONST static const
#define FC_LOG2(x) std::log2(x)
#define FC_EXP2(x) std::exp2(x)
#define FC_POW(x, y) std::pow(x, y)
#define FC_FLOOR(x) std::floor(x)
#define FC_ISFINITE(x) std::isfinite(x)
#endif

// Source ids, in the order shown in the "Source" menu.
#define FC_SRC_REC709 0
#define FC_SRC_REC2020 1
#define FC_SRC_APPLE_G20 2
#define FC_SRC_ACESCCT 3
#define FC_SRC_DWG_REC709 4
#define FC_SRC_DWG_DI 5
#define FC_SRC_CINEON 6
#define FC_NUM_SOURCES 7

#define FC_BAND_EPS 1e-6f  // keeps exact band edges (k/15, e.g. 17/255) from float rounding

typedef struct { float m[9]; } FCMat3;  // row-major, source linear RGB -> Rec.709 linear RGB
typedef struct { float r, g, b, a; } FCPixel;

// Shared by the CPU renderer and the GPU kernels (all members are 4-byte, same layout everywhere).
typedef struct {
    int w, h;                  // render window size
    int srcBase, srcRow;       // in floats
    int dstBase, dstRow;       // in floats
    int winX0, winY0;          // render window origin, frame pixel coords (OFX: y up)
    int frameTop;              // first row above the frame (frame y1), pixel coords
    int frameX0;               // frame x1
    int source;                // FC_SRC_*
    float mix;
    FCMat3 mat;
    int matIdentity;           // 1: skip the matrix
    int ovEnabled;             // scale overlay
    int ovX, ovY, ovW, ovH;    // overlay rectangle, from the frame's top-left corner
} FCParams;

FC_CONST float FC_PAL[15][3] = FC_PALETTE_INIT;

FC_INLINE float fc_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

FC_INLINE int fc_is_scene_referred(int src) {
    return src == FC_SRC_ACESCCT || src == FC_SRC_DWG_DI || src == FC_SRC_CINEON;
}

// ---- 1. decode: signal -> linear -------------------------------------------------------------
FC_INLINE float fc_decode_acescct(float v) {  // ACES S-2016-001
    if (v <= 0.155251141552511f) return (v - 0.0729055341958355f) / 10.5402377416545f;
    if (v < 1.46799631f) return FC_EXP2(v * 17.52f - 9.72f);
    return 65504.0f;
}
FC_INLINE float fc_decode_davinci_intermediate(float v) {  // Blackmagic "Wide Gamut Intermediate" note
    if (v > 2.0f) v = 2.0f;  // far above white: keeps exp2 finite (still band 14)
    if (v <= 0.02740668f) return v / 10.44426855f;
    return FC_EXP2(v / 0.07329248f - 7.0f) - 0.0075f;
}
FC_INLINE float fc_decode_cineon(float v) {  // Kodak Cineon: ref white 685, ref black 95, 0.002 D/cv, gamma 0.6
    if (v > 2.0f) v = 2.0f;  // far above white: keeps the result finite (still band 14)
    const float black = 0.0108f;             // 10^((95-685)*0.002/0.6)
    float e = FC_EXP2((v * 1023.0f - 685.0f) * (0.002f / 0.6f) * 3.32192809489f);  // 10^x
    return (e - black) / (1.0f - black);
}
FC_INLINE float fc_decode(int src, float v) {
    switch (src) {
        case FC_SRC_REC2020:
        case FC_SRC_DWG_REC709: return v > 0.0f ? FC_POW(v, 2.4f) : 0.0f;
        case FC_SRC_APPLE_G20: return v > 0.0f ? v * v : 0.0f;
        case FC_SRC_ACESCCT: return fc_decode_acescct(v);
        case FC_SRC_DWG_DI: return fc_decode_davinci_intermediate(v);
        case FC_SRC_CINEON: return fc_decode_cineon(v);
        default: return v;
    }
}

// ---- 4. encode: linear [0,1] -> Rec.709 video signal ------------------------------------------
FC_INLINE float fc_oetf_bt709(float l) { return l < 0.018f ? 4.5f * l : 1.099f * FC_POW(l, 0.45f) - 0.099f; }
FC_INLINE float fc_encode(int src, float l) {
    if (fc_is_scene_referred(src)) return fc_oetf_bt709(l);
    return l > 0.0f ? FC_POW(l, 1.0f / 2.4f) : 0.0f;
}

FC_INLINE float fc_luma709(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

// Display luminance (0..1) of a pixel, or -1 when the pixel is NaN/Inf.
FC_INLINE float fc_measure(int src, FCMat3 m, int matIdentity, float r, float g, float b) {
    if (!(FC_ISFINITE(r) && FC_ISFINITE(g) && FC_ISFINITE(b))) return -1.0f;
    if (src == FC_SRC_REC709) return fc_luma709(fc_clamp01(r), fc_clamp01(g), fc_clamp01(b));
    float lr = fc_decode(src, r), lg = fc_decode(src, g), lb = fc_decode(src, b);
    if (!matIdentity) {
        float tr = m.m[0] * lr + m.m[1] * lg + m.m[2] * lb;
        float tg = m.m[3] * lr + m.m[4] * lg + m.m[5] * lb;
        float tb = m.m[6] * lr + m.m[7] * lg + m.m[8] * lb;
        lr = tr; lg = tg; lb = tb;
    }
    if (!(FC_ISFINITE(lr) && FC_ISFINITE(lg) && FC_ISFINITE(lb))) return -1.0f;
    return fc_luma709(fc_encode(src, fc_clamp01(lr)), fc_encode(src, fc_clamp01(lg)), fc_encode(src, fc_clamp01(lb)));
}

FC_INLINE int fc_band(float y) {
    if (!(y > 0.0f)) return 0;
    if (y > 1.0f) y = 1.0f;
    int b = (int)FC_FLOOR(y * 15.0f + FC_BAND_EPS);
    return b > 14 ? 14 : (b < 0 ? 0 : b);
}

FC_INLINE FCPixel fc_shade(FCPixel in, int src, FCMat3 m, int matIdentity, float mix) {
    float y = fc_measure(src, m, matIdentity, in.r, in.g, in.b);
    float fr = 0.0f, fg = 0.0f, fb = 0.0f;  // NaN/Inf -> black
    if (y >= 0.0f) {
        int band = fc_band(y);
        fr = FC_PAL[band][0]; fg = FC_PAL[band][1]; fb = FC_PAL[band][2];
    }
    float ir = FC_ISFINITE(in.r) ? in.r : 0.0f;
    float ig = FC_ISFINITE(in.g) ? in.g : 0.0f;
    float ib = FC_ISFINITE(in.b) ? in.b : 0.0f;
    FCPixel o;
    o.r = ir * (1.0f - mix) + fr * mix;  // exact at mix 0 and 1
    o.g = ig * (1.0f - mix) + fg * mix;
    o.b = ib * (1.0f - mix) + fb * mix;
    o.a = FC_ISFINITE(in.a) ? in.a : 1.0f;
    return o;
}

// Straight-alpha "over" of an RGBA8 overlay texel onto the result (alpha kept from the image).
FC_INLINE FCPixel fc_blend_overlay(FCPixel c, float r8, float g8, float b8, float a8) {
    float a = a8 / 255.0f;
    c.r = c.r * (1.0f - a) + (r8 / 255.0f) * a;
    c.g = c.g * (1.0f - a) + (g8 / 255.0f) * a;
    c.b = c.b * (1.0f - a) + (b8 / 255.0f) * a;
    return c;
}

// Overlay texel index for frame pixel (x, y) (OFX y up), or -1 outside the overlay.
FC_INLINE int fc_overlay_index(int ovEnabled, int ovX, int ovY, int ovW, int ovH, int frameX0, int frameTop, int x, int y) {
    if (!ovEnabled) return -1;
    int col = x - frameX0 - ovX;
    int row = (frameTop - 1 - y) - ovY;
    if (col < 0 || row < 0 || col >= ovW || row >= ovH) return -1;
    return row * ovW + col;
}

#endif  // FC_SHARED_H
