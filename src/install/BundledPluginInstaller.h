#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// Installs the curated free plugin pack on first launch (or from the
// Help menu afterwards) by fetching a manifest from the PluginsProxy
// Lambda, downloading each archive via short-lived presigned S3 URLs,
// verifying SHA-256, and extracting the .component bundles into
// ~/Library/Audio/Plug-Ins/Components/. Tracks what it installed via
// plugins-installed.json so Settings → Plugins can later uninstall.
//
// Design invariants:
//   - Only the user Components folder. No /Library writes, no sudo.
//   - Never overwrite an existing bundle without matching the local
//     install manifest's slug — prevents clobbering a third-party
//     plugin that happens to share a bundle name with ours.
//   - Network + disk work happens on a dedicated thread; UI polls
//     getProgress() and the completion callback fires on the message
//     thread so the dialog can update / close without cross-thread.
//
// Lifetime: caller owns the instance. Do not reuse after a successful
// install (create a fresh one if needed for a re-install). See
// docs/BUNDLED_PLUGINS.md for the full flow.

class BundledPluginInstaller : public juce::Thread {
public:
    struct ArchiveInfo {
        std::string slug;
        std::string version;
        std::string archiveName;
        std::string archiveUrl;            // presigned S3 GET URL (1h TTL)
        juce::int64 archiveSize = 0;
        std::string archiveSha256;
        std::vector<std::string> components;    // .component bundles in the archive
        std::vector<std::string> supportPaths;  // top-level dirs that go to
                                                 // ~/Library/Application Support/
                                                 // (e.g. "Surge XT" → factory content)
    };

    struct Progress {
        enum State {
            Idle,
            FetchingManifest,
            Downloading,
            Verifying,
            Extracting,
            Done,
            Error,
            Cancelled,
        };
        State state = Idle;
        int archivesCompleted = 0;
        int totalArchives = 0;
        juce::int64 bytesDownloaded = 0;
        juce::int64 bytesTotal = 0;
        std::string currentArchive;
        std::string errorMessage;
    };

    using CompletionCallback = std::function<void(bool success, const std::string& message)>;

    BundledPluginInstaller();
    ~BundledPluginInstaller() override;

    // Start on a background thread. onComplete fires on the message
    // thread exactly once, with success = true iff every archive
    // installed without error.
    void startInstall(CompletionCallback onComplete);

    // Request cancellation. Cleanly stops at the next checkpoint —
    // does not roll back components already installed.
    void requestCancel();

    // Thread-safe snapshot for UI polling.
    Progress getProgress() const;

    // Install manifest path: ~/Library/Application Support/
    //   com.performance.app/plugins-installed.json. Present + non-empty
    // means first-run install has run at least once.
    static juce::File installManifestFile();
    static bool isInstalled();

    // User Components directory: ~/Library/Audio/Plug-Ins/Components/.
    static juce::File componentsDirectory();

    // One entry per archive group in the install manifest. Read from
    // disk each call; cheap (~1 KB JSON).
    struct InstalledArchive {
        std::string slug;
        std::string version;
        std::vector<juce::File> installedPaths;
    };
    static std::vector<InstalledArchive> readInstalledManifest();

    // Uninstall one archive group: deletes each installedPath, then
    // rewrites the manifest without this entry. If this was the last
    // entry, the manifest file is deleted so isInstalled() → false.
    // Returns number of bundles actually removed.
    static int uninstallArchive(const std::string& slug);

    // Uninstall everything the installer placed: deletes every
    // installedPath across every archive, then deletes the manifest.
    // Returns number of bundles removed.
    static int uninstallAll();

    // Test-only: redirect the install-manifest path (normally at
    // ~/Library/Application Support/com.performance.app/plugins-installed.json).
    // Passing an empty File restores the default.
    static void setInstallManifestFileForTests(juce::File override);

    void run() override;

private:
    bool fetchManifest(std::vector<ArchiveInfo>& out, std::string& err);
    bool downloadArchive(const ArchiveInfo& info,
                         const juce::File& dest,
                         std::string& err);
    bool verifyArchive(const juce::File& archive,
                       const std::string& expectedSha,
                       std::string& err);
    bool extractAndInstall(const juce::File& archive,
                           const ArchiveInfo& info,
                           std::vector<juce::File>& installedOut,
                           std::string& err);

    // ~/Library/Application Support/ (where Surge XT / Dexed / etc.
    // expect to find their factory content).
    static juce::File applicationSupportDirectory();
    void writeInstallManifest(const std::vector<ArchiveInfo>& archives,
                              const std::vector<std::vector<juce::File>>& installedPerArchive);

    // Progress helpers — all take the mutex internally.
    void setState(Progress::State s);
    void setCurrentArchive(const std::string& name, juce::int64 sizeBytes);
    void addDownloadedBytes(juce::int64 delta);
    void markArchiveDone();
    void setError(const std::string& msg);

    mutable std::mutex progressMutex;
    Progress progress;

    CompletionCallback completionCallback;
    std::atomic<bool> cancelRequested { false };
};
