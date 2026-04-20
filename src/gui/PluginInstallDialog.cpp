#include "gui/PluginInstallDialog.h"
#include "gui/Theme.h"
#include "engine/Log.h"

namespace {

juce::String friendly(BundledPluginInstaller::Progress::State s) {
    using S = BundledPluginInstaller::Progress::State;
    switch (s) {
        case S::Idle:             return "Starting...";
        case S::FetchingManifest: return "Fetching plugin list...";
        case S::Downloading:      return "Downloading";
        case S::Verifying:        return "Verifying";
        case S::Extracting:       return "Installing";
        case S::Done:             return "Done";
        case S::Error:            return "Error";
        case S::Cancelled:        return "Cancelled";
    }
    return {};
}

juce::String formatSize(juce::int64 bytes) {
    if (bytes <= 0) return "0 MB";
    double mb = (double) bytes / (1024.0 * 1024.0);
    if (mb < 1.0) return juce::String(bytes / 1024) + " KB";
    return juce::String(mb, 1) + " MB";
}

}  // namespace

PluginInstallDialog::PluginInstallDialog()
    : progressBar(progressValue) {
    setOpaque(true);

    titleLabel.setText("Installing free plugin pack", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(Theme::fontSizeLg, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId,
                         juce::Colour(Theme::Color::textPrimary));
    addAndMakeVisible(titleLabel);

    statusLabel.setFont(juce::FontOptions(Theme::fontSizeMd));
    statusLabel.setColour(juce::Label::textColourId,
                          juce::Colour(Theme::Color::textSecondary));
    addAndMakeVisible(statusLabel);

    progressLabel.setFont(juce::FontOptions(Theme::fontSizeSm));
    progressLabel.setColour(juce::Label::textColourId,
                            juce::Colour(Theme::Color::textDim));
    addAndMakeVisible(progressLabel);

    progressBar.setPercentageDisplay(false);
    addAndMakeVisible(progressBar);

    closeButton.onClick = [this]() {
        if (finished) {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(0);
        } else {
            installer.requestCancel();
            closeButton.setEnabled(false);
        }
    };
    addAndMakeVisible(closeButton);

    setSize(420, 180);

    installer.startInstall([this](bool ok, const std::string& msg) {
        onInstallComplete(ok, msg);
    });
    startTimerHz(10);
}

PluginInstallDialog::~PluginInstallDialog() {
    stopTimer();
    installer.requestCancel();
}

void PluginInstallDialog::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(Theme::Color::bgOverlay));
    g.setColour(juce::Colour(Theme::Color::border));
    g.drawRect(getLocalBounds(), 1);
}

void PluginInstallDialog::resized() {
    auto r = getLocalBounds().reduced(Theme::spacingL);
    titleLabel.setBounds(r.removeFromTop(Theme::fontSizeLg + 8));
    r.removeFromTop(Theme::spacingS);
    statusLabel.setBounds(r.removeFromTop(Theme::fontSizeMd + 6));
    r.removeFromTop(Theme::spacingXs);
    progressLabel.setBounds(r.removeFromTop(Theme::fontSizeSm + 4));
    r.removeFromTop(Theme::spacingM);
    progressBar.setBounds(r.removeFromTop(18));
    r.removeFromTop(Theme::spacingL);

    auto buttons = r.removeFromTop(28);
    closeButton.setBounds(buttons.removeFromRight(100));
}

void PluginInstallDialog::timerCallback() {
    auto p = installer.getProgress();

    statusLabel.setText(friendly(p.state)
                           + (p.currentArchive.empty() ? juce::String()
                              : juce::String(" ") + juce::String(p.currentArchive)),
                       juce::dontSendNotification);

    if (p.bytesTotal > 0) {
        progressValue = juce::jlimit(0.0, 1.0,
            (double) p.bytesDownloaded / (double) p.bytesTotal);
    }

    progressLabel.setText(
        formatSize(p.bytesDownloaded) + " / " + formatSize(p.bytesTotal)
            + "   ·   " + juce::String(p.archivesCompleted) + "/"
            + juce::String(p.totalArchives) + " groups",
        juce::dontSendNotification);

    repaint();
}

void PluginInstallDialog::onInstallComplete(bool success, const std::string& message) {
    finished = true;
    stopTimer();

    auto p = installer.getProgress();
    statusLabel.setText(juce::String(message), juce::dontSendNotification);
    if (success) {
        progressValue = 1.0;
        closeButton.setButtonText("Done");
        closeButton.setEnabled(true);
    } else {
        closeButton.setButtonText("Close");
        closeButton.setEnabled(true);
        if (!p.errorMessage.empty()) {
            perfLog("[PluginInstall] install failed: %s\n", p.errorMessage.c_str());
        }
    }
    repaint();
}

void PluginInstallDialog::show() {
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(new PluginInstallDialog());
    o.dialogTitle = "Plugins";
    o.dialogBackgroundColour = juce::Colour(Theme::Color::bgOverlay);
    o.escapeKeyTriggersCloseButton = false;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}
