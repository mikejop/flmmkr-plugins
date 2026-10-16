// test_ocio_util.h - config discovery for the OCIO tests.
// Order: $FALSECOLOR_CONFIG_<1X|20>, configs/aces-*/config.ocio, then OCIO built-in configs
// found through the BuiltinConfigRegistry (UNVERIFIED API/names: see README).
#pragma once
#include <OpenColorIO/OpenColorIO.h>
#include <cstdlib>
#include <fstream>
#include <string>
#include "../ocio_pipeline.h"
namespace OCIO = OCIO_NAMESPACE;

#ifndef FALSECOLOR_SOURCE_DIR
#define FALSECOLOR_SOURCE_DIR "."
#endif

inline bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

// version: 1 = ACES 1.x, 2 = ACES 2.0. Returns "" when none is available.
inline std::string findConfig(int version) {
    const char* env = std::getenv(version == 2 ? "FALSECOLOR_CONFIG_20" : "FALSECOLOR_CONFIG_1X");
    if (env && fileExists(env)) return env;
    std::string p = std::string(FALSECOLOR_SOURCE_DIR) + (version == 2 ? "/configs/aces-2.0/config.ocio" : "/configs/aces-1.x/config.ocio");
    if (fileExists(p)) return p;
    try {
        const auto& reg = OCIO::BuiltinConfigRegistry::Get();
        const char* want = version == 2 ? "aces-v2" : "aces-v1";
        for (size_t i = 0; i < reg.getNumBuiltinConfigs(); ++i) {
            std::string n = reg.getBuiltinConfigName(i);
            if (n.find(want) != std::string::npos && n.find("cg-config") != std::string::npos) return "ocio://" + n;
        }
    } catch (...) {}
    return "";
}
