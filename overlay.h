// overlay.h - "Sistema de Zonas" scale drawn on the left of the frame.
// Built once on the CPU as an RGBA8 image (straight alpha) and composited by the CPU renderer or
// the GPU kernel. Rebuilt only when the frame size or the label switch changes.
#pragma once
#include <cstdint>
#include <vector>

namespace fc {

struct ScaleOverlay {
    int x = 0, y = 0;  // position from the frame's top-left corner, pixels
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // w*h*4, rows top to bottom
    bool valid() const { return w > 0 && h > 0 && rgba.size() == (size_t)w * h * 4; }
};

// Returns an empty overlay when the frame is too small to hold a readable scale.
ScaleOverlay buildScaleOverlay(int frameW, int frameH, bool showLabels);

}  // namespace fc
