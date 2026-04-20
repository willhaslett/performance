#pragma once

#include "install/BundledPluginInstaller.h"
#include <juce_gui_basics/juce_gui_basics.h>

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
    PluginInstallDialog();
    ~PluginInstallDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Convenience: wraps this component in a centered DialogWindow
    // and shows it modally. Deletes itself on close.
    static void show();

private:
    void timerCallback() override;
    void onInstallComplete(bool success, const std::string& message);

    BundledPluginInstaller installer;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label progressLabel;
    juce::ProgressBar progressBar;
    juce::TextButton closeButton { "Cancel" };

    double progressValue = 0.0;
    bool finished = false;
};
