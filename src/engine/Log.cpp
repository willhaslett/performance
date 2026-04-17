#include "engine/Log.h"
#include <ctime>
#include <sys/stat.h>
#include <cstdio>

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
    // Rescue any non-empty prior-session log before truncating, so the
    // telemetry shipper can pick it up. Uses a unique .prev suffix per
    // rescue so multiple unshipped sessions don't clobber each other.
    const char* current = "/tmp/performance.log";
    struct stat st;
    if (stat(current, &st) == 0 && st.st_size > 0) {
        char prev[128];
        snprintf(prev, sizeof(prev), "/tmp/performance.log.%ld.prev",
                 (long)time(nullptr));
        rename(current, prev);
    }
    gLogFile = fopen(current, "w");
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
