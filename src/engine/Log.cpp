#include "engine/Log.h"

static FILE* gLogFile = nullptr;

void initLog() {
    gLogFile = fopen("/tmp/performance.log", "w");
    if (gLogFile)
        setvbuf(gLogFile, nullptr, _IONBF, 0);
}

void perfLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if (gLogFile) {
        va_list args2;
        va_start(args2, fmt);
        vfprintf(gLogFile, fmt, args2);
        va_end(args2);
    }
}
