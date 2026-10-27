// metal_backend.mm - Objective-C++ (ARC). macOS only.
#import <Metal/Metal.h>

#include <map>
#include <mutex>

#include "falsecolor_metal_source.h"  // generated into the build dir
#include "metal_backend.h"

namespace fc {

namespace {
std::mutex gCacheMutex;
std::map<void*, id<MTLComputePipelineState>>& cache() {
    static std::map<void*, id<MTLComputePipelineState>> c;
    return c;
}
}  // namespace

struct MetalRunner::Impl {
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> q = nil;
    id<MTLComputePipelineState> pso = nil;
    id<MTLBuffer> overlay = nil;
    unsigned overlayGen = ~0u;
    id<MTLBuffer> stageBuf[4] = {nil, nil, nil, nil};
    size_t stageCap[4] = {0, 0, 0, 0};
    std::string err;
    bool ok = false;
};

bool MetalRunner::available() {
    id<MTLDevice> d = MTLCreateSystemDefaultDevice();
    return d != nil;
}

MetalRunner::MetalRunner(void* commandQueue) : impl_(new Impl) {
    @autoreleasepool {
        if (commandQueue) {
            impl_->q = (__bridge id<MTLCommandQueue>)commandQueue;
            impl_->dev = impl_->q.device;
        } else {
            impl_->dev = MTLCreateSystemDefaultDevice();
            if (impl_->dev) impl_->q = [impl_->dev newCommandQueue];
        }
        if (!impl_->dev || !impl_->q) { impl_->err = "no Metal device/queue"; return; }
        void* key = (__bridge void*)impl_->dev;
        std::lock_guard<std::mutex> lock(gCacheMutex);
        auto it = cache().find(key);
        if (it != cache().end()) { impl_->pso = it->second; impl_->ok = true; return; }
        NSError* e = nil;
        MTLCompileOptions* opts = [MTLCompileOptions new];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        opts.fastMathEnabled = NO;  // band edges must stay exact
#pragma clang diagnostic pop
        id<MTLLibrary> lib = [impl_->dev newLibraryWithSource:[NSString stringWithUTF8String:kFalseColorMetalSource]
                                                      options:opts
                                                        error:&e];
        if (!lib) { impl_->err = std::string("Metal compile error: ") + (e ? e.localizedDescription.UTF8String : "?"); return; }
        id<MTLFunction> fn = [lib newFunctionWithName:@"falsecolor_kernel"];
        if (!fn) { impl_->err = "kernel function not found"; return; }
        impl_->pso = [impl_->dev newComputePipelineStateWithFunction:fn error:&e];
        if (!impl_->pso) { impl_->err = std::string("pipeline error: ") + (e ? e.localizedDescription.UTF8String : "?"); return; }
        cache()[key] = impl_->pso;
        impl_->ok = true;
    }
}

MetalRunner::~MetalRunner() { delete impl_; }
bool MetalRunner::ok() const { return impl_->ok; }
std::string MetalRunner::lastError() const { return impl_->err; }
std::string MetalRunner::deviceName() const {
    return impl_->dev ? std::string(impl_->dev.name.UTF8String) : std::string("none");
}

bool MetalRunner::setOverlay(const uint8_t* rgba, size_t bytes, unsigned generation) {
    if (!impl_->ok) return false;
    if (generation == impl_->overlayGen && impl_->overlay) return true;
    impl_->overlay = (rgba && bytes) ? [impl_->dev newBufferWithBytes:rgba length:bytes options:MTLResourceStorageModeShared] : nil;
    impl_->overlayGen = generation;
    return rgba == nullptr || impl_->overlay != nil;
}

bool MetalRunner::run(void* srcBuf, void* dstBuf, const FCParams& p) {
    if (!impl_->ok) return false;
    @autoreleasepool {
        if (!impl_->overlay) {  // the kernel always needs something bound at index 3
            const uint32_t zero = 0;
            impl_->overlay = [impl_->dev newBufferWithBytes:&zero length:4 options:MTLResourceStorageModeShared];
        }
        FCParams q = p;
        if (impl_->overlayGen == ~0u) q.ovEnabled = 0;
        id<MTLCommandBuffer> cb = [impl_->q commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        [enc setComputePipelineState:impl_->pso];
        [enc setBuffer:(__bridge id<MTLBuffer>)srcBuf offset:0 atIndex:0];
        [enc setBuffer:(__bridge id<MTLBuffer>)dstBuf offset:0 atIndex:1];
        [enc setBytes:&q length:sizeof(q) atIndex:2];
        [enc setBuffer:impl_->overlay offset:0 atIndex:3];
        [enc dispatchThreads:MTLSizeMake(p.w, p.h, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.status != MTLCommandBufferStatusCompleted) {
            impl_->err = std::string("command buffer failed: ") + (cb.error ? cb.error.localizedDescription.UTF8String : "unknown");
            return false;
        }
    }
    return true;
}

void* MetalRunner::stage(int slot, size_t floats) {
    if (slot < 0 || slot > 3 || !impl_->dev) return nullptr;
    if (impl_->stageCap[slot] < floats) {
        impl_->stageBuf[slot] = [impl_->dev newBufferWithLength:floats * sizeof(float) options:MTLResourceStorageModeShared];
        impl_->stageCap[slot] = impl_->stageBuf[slot] ? floats : 0;
    }
    return (__bridge void*)impl_->stageBuf[slot];
}

float* MetalRunner::stagePtr(int slot) {
    if (slot < 0 || slot > 3 || !impl_->stageBuf[slot]) return nullptr;
    return (float*)impl_->stageBuf[slot].contents;
}

bool MetalRunner::blit(void* hostBuf, int base, int row, int slot, int w, int h, bool toStage) {
    if (!impl_->ok || !impl_->stageBuf[slot]) return false;
    @autoreleasepool {
        id<MTLCommandBuffer> cb = [impl_->q commandBuffer];
        id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
        id<MTLBuffer> hb = (__bridge id<MTLBuffer>)hostBuf;
        const NSUInteger rowBytes = (NSUInteger)w * 4 * sizeof(float);
        for (int y = 0; y < h; ++y) {
            NSUInteger hOff = ((NSUInteger)base + (NSUInteger)y * row) * sizeof(float);
            NSUInteger sOff = (NSUInteger)y * rowBytes;
            if (toStage)
                [bl copyFromBuffer:hb sourceOffset:hOff toBuffer:impl_->stageBuf[slot] destinationOffset:sOff size:rowBytes];
            else
                [bl copyFromBuffer:impl_->stageBuf[slot] sourceOffset:sOff toBuffer:hb destinationOffset:hOff size:rowBytes];
        }
        [bl endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        return cb.status == MTLCommandBufferStatusCompleted;
    }
}

}  // namespace fc
