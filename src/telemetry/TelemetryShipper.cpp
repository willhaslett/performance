#include "telemetry/TelemetryShipper.h"
#include "telemetry/InstallId.h"
#include "api/StateAPI.h"
#include "engine/Log.h"
#include "BuildConfig.h"
#include "BuildVersion.h"
#include <juce_core/juce_core.h>

namespace {

constexpr const char* kConfigKey = "telemetry_enabled";

bool endpointConfigured() {
    // Empty values mean keys/telemetry.json wasn't present at build time.
    return juce::String(TELEMETRY_URL).isNotEmpty()
        && juce::String(TELEMETRY_TOKEN).isNotEmpty();
}

// Gzip-compress raw bytes in memory.
juce::MemoryBlock gzip(const juce::MemoryBlock& raw) {
    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream gz(compressed, 9,
            juce::GZIPCompressorOutputStream::windowBitsGZIP);
        gz.write(raw.getData(), raw.getSize());
    }
    return juce::MemoryBlock(compressed.getData(), compressed.getDataSize());
}

bool postLog(const juce::File& logFile, const juce::String& installId) {
    juce::MemoryBlock raw;
    if (!logFile.loadFileAsData(raw) || raw.getSize() == 0) {
        return false;
    }
    auto body = gzip(raw);

    juce::URL url(TELEMETRY_URL);
    juce::String headers;
    headers << "Authorization: Bearer " << TELEMETRY_TOKEN << "\r\n";
    headers << "Content-Type: application/gzip\r\n";
    headers << "X-Install-Id: " << installId << "\r\n";
    headers << "X-Commit: " << BUILD_GIT_COMMIT << "\r\n";
    headers << "X-Version: " << BUILD_VERSION << "\r\n";

    url = url.withPOSTData(body);

    int statusCode = 0;
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
            .withHttpRequestCmd("POST")
            .withExtraHeaders(headers)
            .withStatusCode(&statusCode)
            .withConnectionTimeoutMs(10000)
    );

    if (!stream || statusCode < 200 || statusCode >= 300) {
        perfLog("[Telemetry] Ship failed: file=%s status=%d\n",
                logFile.getFileName().toRawUTF8(), statusCode);
        return false;
    }

    perfLog("[Telemetry] Shipped %s (%d bytes gzipped, status %d)\n",
            logFile.getFileName().toRawUTF8(),
            (int)body.getSize(), statusCode);
    return true;
}

}  // namespace

namespace Telemetry {

bool isEnabled(StateAPI& state) {
    auto v = state.getConfig(kConfigKey);
    if (v.empty()) return true;  // default on
    return v == "1" || v == "true";
}

void setEnabled(StateAPI& state, bool enabled) {
    state.setConfig(kConfigKey, enabled ? "1" : "0");
}

void shipPrevLogs(StateAPI& state) {
    if (!endpointConfigured()) {
        perfLog("[Telemetry] No endpoint configured — shipper is a no-op\n");
        return;
    }
    if (!isEnabled(state)) {
        perfLog("[Telemetry] Disabled in settings — skipping ship\n");
        return;
    }

    // Fire-and-forget — juce::Thread::launch runs the lambda on a new
    // thread that self-destructs when it returns.
    auto installId = InstallId::id();
    juce::Thread::launch([installId]() {
        juce::File tmp("/tmp");
        auto files = tmp.findChildFiles(juce::File::findFiles, false,
                                         "performance.log.*.prev");
        for (auto& f : files) {
            if (postLog(f, installId)) {
                f.deleteFile();
            }
        }
    });
}

}  // namespace Telemetry
