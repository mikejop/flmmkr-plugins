// cpu_render.h - reference CPU renderer (multithreaded) around fc_shared.h.
#pragma once
#include <cstddef>
#include <cstdint>

#include "fc_shared.h"

namespace fc {

// src/dst are the image buffers' base pointers; p.srcBase/p.dstBase/p.*Row locate the render window
// (exactly as for the GPU kernel). overlay: RGBA8 scale image (p.ovW x p.ovH) or null.
void cpuRender(const float* src, float* dst, const FCParams& p, const uint8_t* overlay);

// Any pixel layout an OFX host may hand over: 1, 2 or 4 bytes per component (8-bit, 16-bit,
// float) and 3 (RGB) or 4 (RGBA) components. srcWin/dstWin point at the first pixel of the render
// window; row strides in bytes (may be negative). Same maths and overlay as cpuRender().
void cpuRenderAny(const uint8_t* srcWin, ptrdiff_t srcRowBytes, uint8_t* dstWin, ptrdiff_t dstRowBytes,
                  int components, int bytesPerComponent, const FCParams& p, const uint8_t* overlay);

// The per-pixel body shared by every CPU caller (and mirrored by the Metal kernel).
FCPixel renderPixel(const float* src, const FCParams& p, const uint8_t* overlay, int gx, int gy);

}  // namespace fc
