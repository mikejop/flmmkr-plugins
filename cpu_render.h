// cpu_render.h - reference CPU renderer (multithreaded) around fc_shared.h.
#pragma once
#include <cstdint>

#include "fc_shared.h"

namespace fc {

// src/dst are the image buffers' base pointers; p.srcBase/p.dstBase/p.*Row locate the render window
// (exactly as for the GPU kernel). overlay: RGBA8 scale image (p.ovW x p.ovH) or null.
void cpuRender(const float* src, float* dst, const FCParams& p, const uint8_t* overlay);

// The per-pixel body shared by every CPU caller (and mirrored by the Metal kernel).
FCPixel renderPixel(const float* src, const FCParams& p, const uint8_t* overlay, int gx, int gy);

}  // namespace fc
