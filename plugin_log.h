// plugin_log.h - tiny logger: stderr + a log file in the user's log folder (never the plugin folder):
//   macOS   ~/Library/Logs/FalseColor.log
//   Windows %LOCALAPPDATA%\FLMMKR\FalseColor.log
//   Linux   $XDG_STATE_HOME/flmmkr/FalseColor.log (default ~/.local/state/flmmkr/FalseColor.log)
#pragma once
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

namespace fc {

inline std::string logFilePath() {
    namespace fs = std::filesystem;
    fs::path dir;
#if defined(_WIN32)
    if (const char* a = std::getenv("LOCALAPPDATA")) dir = fs::path(a) / "FLMMKR";
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) dir = fs::path(h) / "Library" / "Logs";
#else
    if (const char* x = std::getenv("XDG_STATE_HOME")) dir = fs::path(x) / "flmmkr";
    else if (const char* h = std::getenv("HOME")) dir = fs::path(h) / ".local" / "state" / "flmmkr";
#endif
    if (dir.empty()) return std::string();
    std::error_code ec;
    fs::create_directories(dir, ec);
    try {  // Windows: a user name that is not representable in the narrow code page -> no log file
        return (dir / "FalseColor.log").string();
    } catch (...) {
        return std::string();
    }
}

inline void logLine(const std::string& msg) {
    static std::mutex m;
    static const std::string path = logFilePath();
    std::lock_guard<std::mutex> lock(m);
    std::fprintf(stderr, "[FalseColor] %s\n", msg.c_str());
    if (!path.empty())
        if (FILE* f = std::fopen(path.c_str(), "a")) {
            std::fprintf(f, "%s\n", msg.c_str());
            std::fclose(f);
        }
}

inline bool verboseLog() {
    static const bool v = std::getenv("FALSECOLOR_VERBOSE") != nullptr;
    return v;
}

}  // namespace fc
