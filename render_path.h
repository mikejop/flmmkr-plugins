// render_path.h - "Render Path" selection and per-frame GPU->CPU fallback. Header-only, no deps.
#pragma once
#include <functional>
#include <string>

namespace fc {

enum class PathChoice { Auto = 0, Cpu = 1, Gpu = 2 };
enum class Backend { Cpu, Metal };

// Auto: GPU when the host offers Metal, CPU otherwise. Explicit GPU without host Metal -> CPU.
inline Backend chooseBackend(PathChoice choice, bool hostMetalAvailable) {
    switch (choice) {
        case PathChoice::Cpu: return Backend::Cpu;
        case PathChoice::Gpu:
        case PathChoice::Auto:
        default: return hostMetalAvailable ? Backend::Metal : Backend::Cpu;
    }
}

struct RunResult {
    Backend used;
    bool fellBack;
    std::string error;
};

// Runs `gpu` when backend == Metal; if it returns false or throws, runs `cpu` for this frame.
inline RunResult runWithFallback(Backend backend, const std::function<bool(std::string&)>& gpu,
                                 const std::function<void()>& cpu) {
    if (backend == Backend::Metal) {
        std::string err;
        bool ok = false;
        try { ok = gpu(err); } catch (const std::exception& e) { err = e.what(); } catch (...) { err = "unknown GPU exception"; }
        if (ok) return {Backend::Metal, false, ""};
        cpu();
        return {Backend::Cpu, true, err.empty() ? "GPU render failed" : err};
    }
    cpu();
    return {Backend::Cpu, false, ""};
}

}  // namespace fc
