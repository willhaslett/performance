#pragma once

#include "install/BundledPluginInstaller.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Themed modal shown while the bundled-plugin installer runs. Polls
// the installer's progress snapshot on a 10Hz timer, updates status
// text + progress bar, and closes itself on completion. Kicks off
// the install in its constructor — callers just launch the dialog.
//
// Used from two entry points:
//   - First-launch auto-trigger (see main.mm startup sequence)
//   - Help → Install Plugin Pack… menu item
class PluginInstallDialog : public juce::Component,
                            private juce::Timer {
public:
    // rescanAndSync may be null; if provided, the dialog invokes it on
    // a worker after a successful install so newly-installed AU bundles
    // appear in the app's plugin catalog without a restart. The caller
    // supplies the callback because it's the only one who can atomically
    // rescan the engine and sync the coordinator's plugin catalog.
    using RescanFn = std::function<void()>;
    explicit PluginInstallDialog(RescanFn rescanAndSync = {});
    ~PluginInstallDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Convenience: wraps this component in a centered DialogWindow
    // and shows it modally. Deletes itself on close.
    static void show(RescanFn rescanAndSync = {});

private:
    void timerCallback() override;
    void onInstallComplete(bool success, const std::string& message);
    void startPostInstallRescan();

    RescanFn rescanAndSync;
    BundledPluginInstaller installer;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label progressLabel;
    juce::ProgressBar progressBar;
    juce::TextButton closeButton { "Cancel" };

    double progressValue = 0.0;
    bool finished = false;
};
