// Generates palette_generated.h from kPalette255 in falsecolor_core.cpp.
// Build: clang++ -std=c++17 -DFC_GENERATOR -I. tools/gen_palette.cpp falsecolor_core.cpp -o gen_palette
#include <cstdio>
#include "falsecolor_core.h"

int main() {
    std::printf("// GENERATED from falsecolor_core.cpp (kPalette255 / 255). Do not edit by hand.\n");
    std::printf("// Usage: constant float PAL[15][3] = FC_PALETTE_INIT;  (C++/Metal/OpenCL/CUDA)\n");
    std::printf("#define FC_PALETTE_INIT { \\\n");
    for (int i = 0; i < fc::kNumBands; ++i) {
        std::printf("  { %.9gf, %.9gf, %.9gf }%s \\\n", fc::kPalette255[i][0] / 255.0f,
                    fc::kPalette255[i][1] / 255.0f, fc::kPalette255[i][2] / 255.0f,
                    i + 1 < fc::kNumBands ? "," : "");
    }
    std::printf("}\n");
    return 0;
}
