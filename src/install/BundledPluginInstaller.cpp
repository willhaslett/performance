#include "install/BundledPluginInstaller.h"
#include "engine/Log.h"
#include "BuildConfig.h"
#include "BuildVersion.h"
#include "telemetry/InstallId.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_cryptography/juce_cryptography.h>

namespace {

constexpr const char* kInstallManifestName = "plugins-installed.json";
constexpr int kDownloadChunkBytes = 64 * 1024;
constexpr int kHttpConnectionTimeoutMs = 15000;

juce::File appSupportDir() {
    // On macOS, JUCE's userApplicationDataDirectory is ~/Library —
    // the "Application Support" segment is by convention appended by
    // callers. See telemetry/InstallId.cpp for the matching pattern.
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("Application Support")
                  .getChildFile("com.performance.app");
    dir.createDirectory();
    return dir;
}

juce::String hexDigest(const juce::SHA256& sha) {
    return sha.toHexString();
}

bool endpointConfigured() {
    return juce::String(PLUGINS_PROXY_URL).isNotEmpty()
        && juce::String(TELEMETRY_TOKEN).isNotEmpty();
}

// Parse the server manifest JSON into ArchiveInfo objects. The
// archiveUrl field is a presigned S3 URL filled in by the Lambda.
bool parseManifest(const juce::String& body,
                   std::vector<BundledPluginInstaller::ArchiveInfo>& out,
                   std::string& err) {
    auto root = juce::JSON::parse(body);
    if (!root.isObject()) {
        err = "manifest is not a JSON object";
        return false;
    }
    auto archives = root.getProperty("archives", juce::var());
    if (!archives.isArray()) {
        err = "manifest has no archives array";
        return false;
    }
    for (int i = 0; i < archives.size(); ++i) {
        auto a = archives[i];
        BundledPluginInstaller::ArchiveInfo info;
        info.slug = a.getProperty("slug", "").toString().toStdString();
        info.version = a.getProperty("version", "").toString().toStdString();
        info.archiveName = a.getProperty("archiveName", "").toString().toStdString();
        info.archiveUrl = a.getProperty("archiveUrl", "").toString().toStdString();
        info.archiveSize = (juce::int64) (int64_t) a.getProperty("archiveSize", 0);
        info.archiveSha256 = a.getProperty("archiveSha256", "").toString().toStdString();

        auto comps = a.getProperty("components", juce::var());
        if (comps.isArray()) {
            for (int j = 0; j < comps.size(); ++j) {
                info.components.push_back(comps[j].toString().toStdString());
            }
        }

        if (info.archiveName.empty() || info.archiveUrl.empty()
            || info.archiveSha256.empty() || info.components.empty()) {
            err = "manifest entry " + std::to_string(i) + " is incomplete";
            return false;
        }
        out.push_back(std::move(info));
    }
    return true;
}

}  // namespace

BundledPluginInstaller::BundledPluginInstaller()
    : juce::Thread("BundledPluginInstaller") {}

BundledPluginInstaller::~BundledPluginInstaller() {
    cancelRequested = true;
    stopThread(5000);
}

void BundledPluginInstaller::startInstall(CompletionCallback onComplete) {
    completionCallback = std::move(onComplete);
    startThread();
}

void BundledPluginInstaller::requestCancel() {
    cancelRequested = true;
}

BundledPluginInstaller::Progress BundledPluginInstaller::getProgress() const {
    std::lock_guard<std::mutex> lock(progressMutex);
    return progress;
}

juce::File BundledPluginInstaller::installManifestFile() {
    return appSupportDir().getChildFile(kInstallManifestName);
}

bool BundledPluginInstaller::isInstalled() {
    auto f = installManifestFile();
    return f.existsAsFile() && f.getSize() > 0;
}

juce::File BundledPluginInstaller::componentsDirectory() {
    auto lib = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                  .getChildFile("Library/Audio/Plug-Ins/Components");
    return lib;
}

// -----------------------------------------------------------------------------
// Thread body
// -----------------------------------------------------------------------------

void BundledPluginInstaller::run() {
    auto finish = [this](bool ok, const std::string& msg) {
        if (completionCallback) {
            auto cb = completionCallback;
            juce::MessageManager::callAsync([cb, ok, msg]() { cb(ok, msg); });
        }
    };

    if (!endpointConfigured()) {
        setError("Plugin server not configured in this build");
        setState(Progress::Error);
        finish(false, "Plugin server not configured in this build.");
        return;
    }

    setState(Progress::FetchingManifest);

    std::vector<ArchiveInfo> archives;
    {
        std::string err;
        if (!fetchManifest(archives, err)) {
            setError(err);
            setState(Progress::Error);
            finish(false, "Couldn't reach plugin server: " + err);
            return;
        }
    }

    if (archives.empty()) {
        setError("manifest had no archives");
        setState(Progress::Error);
        finish(false, "Plugin server returned an empty manifest.");
        return;
    }

    juce::int64 totalBytes = 0;
    for (auto& a : archives) totalBytes += a.archiveSize;
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        progress.totalArchives = (int) archives.size();
        progress.bytesTotal = totalBytes;
    }

    auto components = componentsDirectory();
    if (!components.createDirectory()) {
        setError("couldn't create " + components.getFullPathName().toStdString());
        setState(Progress::Error);
        finish(false, "Couldn't create the Components folder.");
        return;
    }

    auto scratch = appSupportDir().getChildFile("plugin-install-scratch");
    scratch.deleteRecursively();
    scratch.createDirectory();

    std::vector<std::vector<juce::File>> installedPerArchive;

    for (auto& archive : archives) {
        if (cancelRequested) {
            setState(Progress::Cancelled);
            scratch.deleteRecursively();
            finish(false, "Install cancelled.");
            return;
        }

        setCurrentArchive(archive.slug, archive.archiveSize);
        setState(Progress::Downloading);

        auto zipFile = scratch.getChildFile(juce::String(archive.archiveName));
        std::string err;
        if (!downloadArchive(archive, zipFile, err)) {
            setError("download failed for " + archive.slug + ": " + err);
            setState(Progress::Error);
            scratch.deleteRecursively();
            finish(false, "Download failed for " + archive.slug + ": " + err);
            return;
        }

        setState(Progress::Verifying);
        if (!verifyArchive(zipFile, archive.archiveSha256, err)) {
            setError("checksum mismatch on " + archive.slug + ": " + err);
            setState(Progress::Error);
            scratch.deleteRecursively();
            finish(false, "Checksum mismatch on " + archive.slug + ".");
            return;
        }

        setState(Progress::Extracting);
        std::vector<juce::File> installed;
        if (!extractAndInstall(zipFile, archive.components, installed, err)) {
            setError("install failed for " + archive.slug + ": " + err);
            setState(Progress::Error);
            scratch.deleteRecursively();
            finish(false, "Install failed for " + archive.slug + ": " + err);
            return;
        }

        installedPerArchive.push_back(std::move(installed));
        zipFile.deleteFile();
        markArchiveDone();
    }

    scratch.deleteRecursively();
    writeInstallManifest(archives, installedPerArchive);

    setState(Progress::Done);
    finish(true, "Installed " + std::to_string(archives.size()) + " plugin groups.");
}

// -----------------------------------------------------------------------------
// Manifest fetch
// -----------------------------------------------------------------------------

bool BundledPluginInstaller::fetchManifest(std::vector<ArchiveInfo>& out,
                                            std::string& err) {
    juce::URL url(PLUGINS_PROXY_URL);

    juce::String headers;
    headers << "Authorization: Bearer " << TELEMETRY_TOKEN << "\r\n";
    headers << "X-Install-Id: " << InstallId::id() << "\r\n";
    headers << "X-Version: " << BUILD_VERSION << "\r\n";

    int statusCode = 0;
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd("GET")
            .withExtraHeaders(headers)
            .withStatusCode(&statusCode)
            .withConnectionTimeoutMs(kHttpConnectionTimeoutMs));

    if (!stream) {
        err = "no response";
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        err = "HTTP " + std::to_string(statusCode);
        return false;
    }

    auto body = stream->readEntireStreamAsString();
    return parseManifest(body, out, err);
}

// -----------------------------------------------------------------------------
// Archive download
// -----------------------------------------------------------------------------

bool BundledPluginInstaller::downloadArchive(const ArchiveInfo& info,
                                              const juce::File& dest,
                                              std::string& err) {
    dest.deleteFile();

    juce::URL url(juce::String(info.archiveUrl));

    int statusCode = 0;
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd("GET")
            .withStatusCode(&statusCode)
            .withConnectionTimeoutMs(kHttpConnectionTimeoutMs));

    if (!stream) {
        err = "connection failed";
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        err = "HTTP " + std::to_string(statusCode);
        return false;
    }

    juce::FileOutputStream out(dest);
    if (!out.openedOk()) {
        err = "couldn't open " + dest.getFullPathName().toStdString();
        return false;
    }

    juce::HeapBlock<char> buffer(kDownloadChunkBytes);
    while (!stream->isExhausted()) {
        if (cancelRequested) {
            err = "cancelled";
            return false;
        }
        auto n = stream->read(buffer.getData(), kDownloadChunkBytes);
        if (n <= 0) break;
        out.write(buffer.getData(), (size_t) n);
        addDownloadedBytes(n);
    }
    out.flush();
    return true;
}

// -----------------------------------------------------------------------------
// SHA-256 verification
// -----------------------------------------------------------------------------

bool BundledPluginInstaller::verifyArchive(const juce::File& archive,
                                            const std::string& expectedSha,
                                            std::string& err) {
    juce::FileInputStream in(archive);
    if (!in.openedOk()) {
        err = "couldn't open archive for verification";
        return false;
    }
    juce::SHA256 sha(in);
    auto actual = hexDigest(sha);
    if (!actual.equalsIgnoreCase(juce::String(expectedSha))) {
        err = "expected " + expectedSha + " got " + actual.toStdString();
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Extract + install
// -----------------------------------------------------------------------------

bool BundledPluginInstaller::extractAndInstall(
    const juce::File& archive,
    const std::vector<std::string>& expectedComponents,
    std::vector<juce::File>& installedOut,
    std::string& err) {

    juce::FileInputStream zipIn(archive);
    if (!zipIn.openedOk()) {
        err = "couldn't open archive";
        return false;
    }
    juce::ZipFile zip(&zipIn, false);

    // Extract the whole zip into a per-archive scratch dir.
    auto extractDir = archive.getParentDirectory()
                          .getChildFile(archive.getFileNameWithoutExtension() + "-extracted");
    extractDir.deleteRecursively();
    extractDir.createDirectory();

    auto result = zip.uncompressTo(extractDir, true);
    if (result.failed()) {
        err = result.getErrorMessage().toStdString();
        return false;
    }

    auto components = componentsDirectory();
    for (auto& compName : expectedComponents) {
        auto src = extractDir.getChildFile(juce::String(compName));
        if (!src.isDirectory()) {
            err = "archive missing expected component " + compName;
            return false;
        }

        auto dest = components.getChildFile(juce::String(compName));
        if (dest.exists()) {
            // Replace unconditionally: on re-install we want the new
            // version, and the bundle names we ship are distinctive
            // enough that a collision with a user-installed plugin
            // of the same name is unlikely. (If we ever ship a bundle
            // whose name *could* collide — e.g. we bundle Surge XT and
            // the user installed their own copy — revisit this with
            // a slug-matching check against plugins-installed.json.)
            dest.deleteRecursively();
        }
        if (!src.copyDirectoryTo(dest)) {
            err = "copy failed: " + compName;
            return false;
        }
        installedOut.push_back(dest);
    }

    extractDir.deleteRecursively();
    return true;
}

// -----------------------------------------------------------------------------
// Local install manifest
// -----------------------------------------------------------------------------

void BundledPluginInstaller::writeInstallManifest(
    const std::vector<ArchiveInfo>& archives,
    const std::vector<std::vector<juce::File>>& installedPerArchive) {

    juce::DynamicObject::Ptr root(new juce::DynamicObject());
    root->setProperty("version", 1);
    root->setProperty("installedAt", juce::Time::getCurrentTime().toISO8601(true));

    juce::Array<juce::var> archiveArray;
    for (size_t i = 0; i < archives.size(); ++i) {
        auto& a = archives[i];
        juce::DynamicObject::Ptr obj(new juce::DynamicObject());
        obj->setProperty("slug", juce::String(a.slug));
        obj->setProperty("version", juce::String(a.version));
        obj->setProperty("archiveSha256", juce::String(a.archiveSha256));

        juce::Array<juce::var> paths;
        if (i < installedPerArchive.size()) {
            for (auto& f : installedPerArchive[i]) {
                paths.add(f.getFullPathName());
            }
        }
        obj->setProperty("installedPaths", paths);
        archiveArray.add(juce::var(obj.get()));
    }
    root->setProperty("archives", archiveArray);

    auto json = juce::JSON::toString(juce::var(root.get()), true);
    auto file = installManifestFile();
    file.getParentDirectory().createDirectory();
    bool ok = file.replaceWithText(json);
    perfLog("[PluginInstall] wrote install manifest to %s (%d bytes, ok=%d)\n",
            file.getFullPathName().toRawUTF8(), (int)json.length(), (int)ok);
}

// -----------------------------------------------------------------------------
// Progress helpers
// -----------------------------------------------------------------------------

void BundledPluginInstaller::setState(Progress::State s) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.state = s;
}

void BundledPluginInstaller::setCurrentArchive(const std::string& name,
                                                juce::int64 sizeBytes) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.currentArchive = name;
    (void) sizeBytes;  // kept for symmetry with future per-archive UI
}

void BundledPluginInstaller::addDownloadedBytes(juce::int64 delta) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.bytesDownloaded += delta;
}

void BundledPluginInstaller::markArchiveDone() {
    std::lock_guard<std::mutex> lock(progressMutex);
    ++progress.archivesCompleted;
}

void BundledPluginInstaller::setError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.errorMessage = msg;
    perfLog("[PluginInstall] %s\n", msg.c_str());
}
