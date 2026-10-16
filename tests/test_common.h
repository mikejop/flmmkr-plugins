// test_common.h - minimal check helpers (no framework).
#pragma once
#include <cstdio>
#include <cstdlib>
static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_failures; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
#define TEST_END() do { std::printf(g_failures ? "FAILED (%d)\n" : "OK\n", g_failures); return g_failures ? 1 : 0; } while (0)
// ctest treats exit code 77 as "skipped" (SKIP_RETURN_CODE).
#define TEST_SKIP(msg) do { std::printf("SKIP: %s\n", msg); return 77; } while (0)
