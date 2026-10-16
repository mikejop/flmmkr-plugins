// falsecolor_core.h - pure false-color math. No OFX, OCIO or GPU dependencies.
#pragma once
#include <cstddef>

namespace fc {

constexpr int kNumBands = 15;

enum LumaMode { LUMA_REC709 = 0, LUMA_REC2020 = 1 };

// Source of truth for the palette (RGB 0-255). palette_generated.h is generated from it
// (see tools/gen_palette.cpp) and shared with the Metal/OpenCL/CUDA kernels.
extern const int kPalette255[kNumBands][3];
// Float palette (kPalette255 / 255), taken from palette_generated.h.
extern const float kPaletteF[kNumBands][3];

// Tolerance added before floor() so exact band edges (k/15, e.g. 17/255) are not lost to
// float rounding. Identical constant in every GPU kernel.
constexpr float kBandEps = 1e-6f;

float lumaOf(float r, float g, float b, int lumaMode);

// band = min(floor(Y*15), 14), Y clamped to [0,1]; NaN -> 0.
int bandFromY(float y);

// Pipeline steps 3-7. `in` is the original RGBA pixel, `conv` the RGB after the colour-space
// conversion (step 2; equal to `in` for "Display Rec.709"). out may alias neither.
void falseColorPixel(const float in[4], const float conv[3], int lumaMode, float mix, float out[4]);

}  // namespace fc
