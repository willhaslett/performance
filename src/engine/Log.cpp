#include "engine/Log.h"
#include <ctime>

static FILE* gLogFile = nullptr;

static void writeTimestamp(FILE* f) {
    time_t now = time(nullptr);
    struct tm tm;
    gmtime_r(&now, &tm);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ ", &tm);
    fputs(buf, f);
}

void initLog() {
    gLogFile = fopen("/tmp/performance.log", "w");
    if (gLogFile)
        setvbuf(gLogFile, nullptr, _IONBF, 0);
}

void perfLog(const char* fmt, ...) {
    writeTimestamp(stderr);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if (gLogFile) {
        writeTimestamp(gLogFile);
        va_list args2;
        va_start(args2, fmt);
        vfprintf(gLogFile, fmt, args2);
        va_end(args2);
    }
}
