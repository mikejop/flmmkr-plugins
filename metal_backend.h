// metal_backend.h - Metal runner (macOS only). Pure C++ interface; implementation in metal_backend.mm.
// The kernel is compiled at runtime from the embedded source (kernels/falsecolor.metal + fc_shared.h),
// so no offline Metal toolchain is needed to build.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "fc_shared.h"

namespace fc {

class MetalRunner {
public:
    // commandQueue = host's id<MTLCommandQueue> (as void*), or nullptr to create a private
    // device/queue (tests only; the plugin never creates its own GPU context).
    explicit MetalRunner(void* commandQueue);
    ~MetalRunner();
    MetalRunner(const MetalRunner&) = delete;
    MetalRunner& operator=(const MetalRunner&) = delete;
    static bool available();

    bool ok() const;
    std::string deviceName() const;
    std::string lastError() const;

    // Uploads the RGBA8 scale overlay; re-uploads only when `generation` changes.
    bool setOverlay(const uint8_t* rgba, size_t bytes, unsigned generation);

    // Dispatches the kernel and waits for completion. Buffers are id<MTLBuffer> handles.
    bool run(void* srcBuf, void* dstBuf, const FCParams& p);

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
