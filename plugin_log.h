// plugin_log.h - tiny logger: stderr + ~/Library/Logs/FalseColor.log (macOS) / stderr elsewhere.
// Never writes to the plugin install folders.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace fc {

inline void logLine(const std::string& msg) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    std::fprintf(stderr, "[FalseColor] %s\n", msg.c_str());
#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string path = std::string(home) + "/Library/Logs/FalseColor.log";
        if (FILE* f = std::fopen(path.c_str(), "a")) {
            std::fprintf(f, "%s\n", msg.c_str());
            std::fclose(f);
        }
    }
#endif
}

inline bool verboseLog() {
    static const bool v = std::getenv("FALSECOLOR_VERBOSE") != nullptr;
    return v;
}

}  // namespace fc
