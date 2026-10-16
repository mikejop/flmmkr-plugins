// metal_backend.h - Metal runner (macOS only). Pure C++ interface; implementation in metal_backend.mm.
// The kernel is compiled at runtime from the embedded source (kernels/falsecolor.metal), so no
// offline Metal toolchain is required to build.
#pragma once
#include <string>

namespace fc {

struct MetalArgs {
    int w = 0, h = 0;
    int srcBase = 0, srcRow = 0;    // floats
    int convBase = 0, convRow = 0;  // floats
    int dstBase = 0, dstRow = 0;    // floats
    int lumaMode = 0;
    float mix = 1.0f;
};

class MetalRunner {
public:
    // commandQueue = host's id<MTLCommandQueue> (as void*), or nullptr to create a private
    // device/queue (tests only; the plugin never creates its own GPU context).
    explicit MetalRunner(void* commandQueue);
    ~MetalRunner();
    static bool available();

    bool ok() const;
    std::string deviceName() const;
    std::string lastError() const;

    // Dispatches the kernel and waits for completion. Buffers are id<MTLBuffer> handles.
    bool run(void* srcBuf, void* convBuf, void* dstBuf, const MetalArgs& a);

    // Shared-storage staging buffers (slots 0..3): CPU-visible, GPU-readable.
    void* stage(int slot, size_t floats);
    float* stagePtr(int slot);
    // Copies a w x h window between a (possibly private) host buffer and a staging slot.
    bool blit(void* hostBuf, int base, int row, int slot, int w, int h, bool toStage);

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace fc
