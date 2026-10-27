// falsecolor_core.h - C++ side of the false-color math (palette, sources, matrices, labels).
// The per-pixel maths itself lives in fc_shared.h and is shared with the GPU kernels.
// No OFX or GPU dependencies.
#pragma once
#include "fc_shared.h"

namespace fc {

constexpr int kNumBands = 15;

// Source of truth for the palette (RGB 0-255). palette_generated.h is generated from it
// (tools/gen_palette.cpp) and shared with the kernels.
extern const int kPalette255[kNumBands][3];
// Float palette (kPalette255 / 255), taken from palette_generated.h.
extern const float kPaletteF[kNumBands][3];

// "Source" menu entries, in FC_SRC_* order.
extern const char* const kSourceNames[FC_NUM_SOURCES];

// Zone-system meaning of each band (UTF-8, upper case), shown by the scale overlay.
extern const char* const kBandLabels[kNumBands];
// Bands that form the mid-tone / skin zone (blocks 7-9 counted from the bottom).
constexpr int kMidFirst = 6, kMidLast = 8;
// IRE range of band k on the 0-109 ruler.
int bandIreLow(int k);
int bandIreHigh(int k);

// Source linear RGB -> Rec.709 linear RGB (Bradford adaptation when white points differ).
FCMat3 sourceToRec709(int source, bool* identity);

int bandFromY(float y);
float measureY(int source, float r, float g, float b);  // -1 for NaN/Inf

// One pixel through the whole pipeline (CPU reference used by the tests).
void falseColorPixel(const float in[4], int source, float mix, float out[4]);

}  // namespace fc
