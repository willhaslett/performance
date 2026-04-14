#pragma once
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

void initLog();
void perfLog(const char* fmt, ...);

// Crash with log — for invariant violations that indicate programming errors.
// Fires in all builds (not compiled out by NDEBUG like assert()).
#define PERF_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        perfLog("FATAL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while(0)
