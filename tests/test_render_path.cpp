#include <stdexcept>
#include "../render_path.h"
#include "test_common.h"
using namespace fc;
int main() {
    CHECK(chooseBackend(PathChoice::Auto, true) == Backend::Metal, "auto+metal");
    CHECK(chooseBackend(PathChoice::Auto, false) == Backend::Cpu, "auto no metal");
    CHECK(chooseBackend(PathChoice::Cpu, true) == Backend::Cpu, "cpu forced");
    CHECK(chooseBackend(PathChoice::Gpu, true) == Backend::Metal, "gpu forced");
    CHECK(chooseBackend(PathChoice::Gpu, false) == Backend::Cpu, "gpu forced w/o host metal");
    int cpuCalls = 0;
    auto cpu = [&] { ++cpuCalls; };
    auto r = runWithFallback(Backend::Metal, [](std::string&) { return true; }, cpu);
    CHECK(r.used == Backend::Metal && !r.fellBack && cpuCalls == 0, "gpu ok");
    r = runWithFallback(Backend::Metal, [](std::string& e) { e = "kernel error"; return false; }, cpu);
    CHECK(r.used == Backend::Cpu && r.fellBack && cpuCalls == 1 && r.error == "kernel error", "gpu error -> cpu");
    r = runWithFallback(Backend::Metal, [](std::string&) -> bool { throw std::runtime_error("lost"); }, cpu);
    CHECK(r.fellBack && cpuCalls == 2 && r.error == "lost", "gpu exception -> cpu");
    r = runWithFallback(Backend::Cpu, [](std::string&) { return true; }, cpu);
    CHECK(r.used == Backend::Cpu && !r.fellBack && cpuCalls == 3, "cpu path");
    TEST_END();
}
