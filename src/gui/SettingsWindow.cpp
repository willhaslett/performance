#include "gui/SettingsWindow.h"
#include "gui/PluginInstallDialog.h"
#include "install/BundledPluginInstaller.h"
#include "api/EngineAPI.h"
#include "engine/Log.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "engine/Log.h"
#include "telemetry/InstallId.h"
#include "telemetry/TelemetryShipper.h"
#include "BuildVersion.h"
#include "BinaryData.h"

// --- SettingsWindow ---

SettingsWindow::SettingsWindow(StateAPI& state, EngineAPI& engine,
                                std::function<void()> syncPluginCatalog)
    : DocumentWindow("Settings", Theme::color(Theme::Color::bgPanel),
                      DocumentWindow::closeButton),
      audioPage(state, engine),
      aboutPage(state),
      pluginsPage(&engine, std::move(syncPluginCatalog)) {

    tabs.setLookAndFeel(&tabLF);
    tabs.addTab("Audio", Theme::color(Theme::Color::bgApp), &audioPage, false);
    tabs.addTab("MIDI", Theme::color(Theme::Color::bgApp), &midiPage, false);
    tabs.addTab("Plugins", Theme::color(Theme::Color::bgApp), &pluginsPage, false);
    tabs.addTab("About", Theme::color(Theme::Color::bgApp), &aboutPage, false);
    tabs.setTabBarDepth(36);
    tabs.setColour(juce::TabbedComponent::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    tabs.setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);

    setContentNonOwned(&tabs, false);
    setUsingNativeTitleBar(true);
    setResizable(false, false);

    int w = AudioPage::labelWidth + AudioPage::comboWidth + AudioPage::padding * 3;
    // Size to the tallest page so any tab fits without overflow.
    int pageH = std::max(audioPage.getDesiredHeight(), aboutPage.getDesiredHeight());
    int h = pageH + tabs.getTabBarDepth() + getTitleBarHeight();
    centreWithSize(w, h);
}

// --- AudioPage ---

SettingsWindow::AudioPage::AudioPage(StateAPI& state, EngineAPI& engine)
    : state(state), engine(engine) {

    auto setupLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(Theme::font(Theme::fontSizeLg));
        label.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textSecondary));
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
    };

    auto setupCombo = [this](juce::ComboBox& box) {
        box.setColour(juce::ComboBox::backgroundColourId, Theme::color(Theme::Color::bgSlot));
        box.setColour(juce::ComboBox::textColourId, Theme::color(Theme::Color::textPrimary));
        box.setColour(juce::ComboBox::outlineColourId, Theme::color(Theme::Color::border));
        addAndMakeVisible(box);
    };

    setupLabel(outputLabel, "Output Device:");
    setupLabel(inputLabel, "Input Device:");
    setupLabel(bufferLabel, "I/O Buffer Size:");
    setupLabel(sampleRateLabel, "Sample Rate:");
    setupLabel(latencyLabel, "Resulting Latency:");

    latencyValue.setFont(Theme::font(Theme::fontSizeLg));
    latencyValue.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textPrimary));
    addAndMakeVisible(latencyValue);

    setupCombo(outputBox);
    setupCombo(inputBox);
    setupCombo(bufferBox);
    setupCombo(sampleRateBox);

    outputBox.onChange = [this]() { applyOutputDevice(); };
    inputBox.onChange = [this]() { applyInputDevice(); };
    bufferBox.onChange = [this]() { applyBufferSize(); };
    sampleRateBox.onChange = [this]() { applySampleRate(); };

    refresh();
    startTimerHz(2);  // refresh to track external changes
}

int SettingsWindow::AudioPage::getDesiredHeight() const {
    return padding + rowHeight * 5 + padding;
}

void SettingsWindow::AudioPage::refresh() {
    auto& dm = engine.getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();

    // Output devices
    outputBox.clear(juce::dontSendNotification);
    if (auto* type = dm.getCurrentDeviceTypeObject()) {
        auto names = type->getDeviceNames(false);
        for (int i = 0; i < names.size(); ++i)
            outputBox.addItem(names[i], i + 1);
        int idx = names.indexOf(setup.outputDeviceName);
        if (idx >= 0) outputBox.setSelectedItemIndex(idx, juce::dontSendNotification);
    }

    // Input devices
    inputBox.clear(juce::dontSendNotification);
    if (auto* type = dm.getCurrentDeviceTypeObject()) {
        auto names = type->getDeviceNames(true);
        for (int i = 0; i < names.size(); ++i)
            inputBox.addItem(names[i], i + 1);
        int idx = names.indexOf(setup.inputDeviceName);
        if (idx >= 0) inputBox.setSelectedItemIndex(idx, juce::dontSendNotification);
    }

    // Buffer sizes
    bufferBox.clear(juce::dontSendNotification);
    if (auto* device = dm.getCurrentAudioDevice()) {
        auto sizes = device->getAvailableBufferSizes();
        int currentBuf = device->getCurrentBufferSizeSamples();
        for (int i = 0; i < sizes.size(); ++i) {
            bufferBox.addItem(juce::String(sizes[i]) + " samples", i + 1);
            if (sizes[i] == currentBuf)
                bufferBox.setSelectedItemIndex(i, juce::dontSendNotification);
        }

        // Sample rates. This control sets the per-project ENGINE rate; the
        // selection reflects the song's engine rate (stable across the async
        // device switch), not the raw device rate.
        sampleRateBox.clear(juce::dontSendNotification);
        auto rates = device->getAvailableSampleRates();
        double currentRate = device->getCurrentSampleRate();  // actual device rate (for latency display)
        double engineRate = (double)state.getSongSampleRate();
        for (int i = 0; i < rates.size(); ++i) {
            sampleRateBox.addItem(juce::String((int)rates[i]) + " Hz", i + 1);
            if (std::abs(rates[i] - engineRate) < 1.0)
                sampleRateBox.setSelectedItemIndex(i, juce::dontSendNotification);
        }

        // Latency
        float latencyMs = (float)currentBuf / (float)currentRate * 1000.0f;
        latencyValue.setText(juce::String(latencyMs, 1) + " ms (" +
                              juce::String(currentBuf) + " samples @ " +
                              juce::String((int)currentRate) + " Hz)",
                              juce::dontSendNotification);
    }
}

void SettingsWindow::AudioPage::applyOutputDevice() {
    auto& dm = engine.getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    setup.outputDeviceName = outputBox.getText();
    state.setConfig("audio_output_device", outputBox.getText().toStdString());
    dm.setAudioDeviceSetup(setup, true);
    refresh();
}

void SettingsWindow::AudioPage::applyInputDevice() {
    auto& dm = engine.getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    setup.inputDeviceName = inputBox.getText();
    state.setConfig("audio_input_device", inputBox.getText().toStdString());
    dm.setAudioDeviceSetup(setup, true);
    refresh();
}

void SettingsWindow::AudioPage::applyBufferSize() {
    auto& dm = engine.getDeviceManager();
    auto* device = dm.getCurrentAudioDevice();
    if (!device) return;
    auto sizes = device->getAvailableBufferSizes();
    int idx = bufferBox.getSelectedItemIndex();
    if (idx < 0 || idx >= sizes.size()) return;
    auto setup = dm.getAudioDeviceSetup();
    int requested = sizes[idx];
    setup.bufferSize = requested;
    state.setConfig("audio_buffer_size", std::to_string(requested));
    auto err = dm.setAudioDeviceSetup(setup, true);
    int applied = device->getCurrentBufferSizeSamples();
    perfLog("[Engine] applyBufferSize: requested=%d, applied=%d%s%s\n",
            requested, applied,
            (applied != requested) ? " (FALLBACK)" : "",
            err.isEmpty() ? "" : (" err=" + err).toRawUTF8());
    refresh();
}

void SettingsWindow::AudioPage::applySampleRate() {
    auto& dm = engine.getDeviceManager();
    auto* device = dm.getCurrentAudioDevice();
    if (!device) return;
    auto rates = device->getAvailableSampleRates();
    int idx = sampleRateBox.getSelectedItemIndex();
    if (idx < 0 || idx >= rates.size()) return;
    // Set the per-project ENGINE rate; the engine asks the device to follow
    // (AudioEngine::setEngineSampleRate). We no longer poke the device directly
    // — that would fight the per-song rate on the next state sync.
    state.setSongSampleRate((int)rates[idx]);
    refresh();
}

void SettingsWindow::AudioPage::timerCallback() {
    // Could refresh here if external changes are expected
}

void SettingsWindow::AudioPage::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));
}

void SettingsWindow::AudioPage::resized() {
    int y = padding;
    int x = padding;

    constexpr int comboHeight = 28;
    auto layoutRow = [&](juce::Label& label, juce::Component& control) {
        label.setBounds(x, y, labelWidth, rowHeight);
        int comboY = y + (rowHeight - comboHeight) / 2;
        control.setBounds(x + labelWidth + 8, comboY, comboWidth, comboHeight);
        y += rowHeight;
    };

    layoutRow(outputLabel, outputBox);
    layoutRow(inputLabel, inputBox);
    layoutRow(bufferLabel, bufferBox);
    layoutRow(sampleRateLabel, sampleRateBox);
    latencyLabel.setBounds(x, y, labelWidth, rowHeight);
    latencyValue.setBounds(x + labelWidth + 8, y, comboWidth, rowHeight);
}

// --- AboutPage ---

SettingsWindow::AboutPage::AboutPage(StateAPI& s) : state(s) {
    auto setupLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(Theme::font(Theme::fontSizeLg));
        label.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textSecondary));
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
    };

    // Values are read-only TextEditors so testers can select + copy.
    auto setupValue = [this](juce::TextEditor& field, const juce::String& text) {
        field.setText(text, false);
        field.setReadOnly(true);
        field.setCaretVisible(false);
        field.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),
                                  Theme::fontSizeLg, juce::Font::plain));
        field.setColour(juce::TextEditor::textColourId, Theme::color(Theme::Color::textPrimary));
        field.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        field.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        field.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(field);
    };

    setupLabel(versionLabel,           "Version:");
    setupLabel(commitLabel,            "Commit:");
    setupLabel(installIdLabel,         "Install ID:");
    setupLabel(firstSeenLabel,         "First Seen:");
    setupLabel(diagnosticsLabel,       "Send Diagnostics:");
    setupLabel(restoreLastProjectLabel,"Startup:");

    juce::String versionText = BUILD_VERSION;
    if (BUILD_GIT_TAG[0]) versionText += juce::String("  (") + BUILD_GIT_TAG + ")";

    setupValue(versionValue,   versionText);
    setupValue(commitValue,    BUILD_GIT_COMMIT);
    setupValue(installIdValue, InstallId::id());
    setupValue(firstSeenValue, InstallId::firstSeen());

    copyIdButton.onClick = [this] {
        juce::SystemClipboard::copyTextToClipboard(InstallId::id());
        copyIdButton.setButtonText("Copied!");
        juce::Timer::callAfterDelay(1200, [this] { copyIdButton.setButtonText("Copy"); });
    };
    addAndMakeVisible(copyIdButton);

    diagnosticsToggle.setToggleState(Telemetry::isEnabled(state), juce::dontSendNotification);
    diagnosticsToggle.setColour(juce::ToggleButton::textColourId,
                                 Theme::color(Theme::Color::textSecondary));
    diagnosticsToggle.setButtonText("Send anonymous session logs for troubleshooting");
    diagnosticsToggle.onClick = [this] {
        Telemetry::setEnabled(state, diagnosticsToggle.getToggleState());
    };
    addAndMakeVisible(diagnosticsToggle);

    // Default ON when the config key isn't set ("opt out" rather than
    // "opt in") — matches the runtime check in PerformanceCoordinator.
    restoreLastProjectToggle.setToggleState(
        state.getConfig("restore_last_project") != "0", juce::dontSendNotification);
    restoreLastProjectToggle.setColour(juce::ToggleButton::textColourId,
                                        Theme::color(Theme::Color::textSecondary));
    restoreLastProjectToggle.setButtonText("Open last project on startup");
    restoreLastProjectToggle.onClick = [this] {
        state.setConfig("restore_last_project",
                        restoreLastProjectToggle.getToggleState() ? "1" : "0");
    };
    addAndMakeVisible(restoreLastProjectToggle);
}

int SettingsWindow::AboutPage::getDesiredHeight() const {
    // Six rows: version, commit, install id, first seen, diagnostics,
    // restore last project. Same row metric as the audio page.
    return AudioPage::padding + AudioPage::rowHeight * 6 + AudioPage::padding;
}

void SettingsWindow::AboutPage::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));
}

void SettingsWindow::AboutPage::resized() {
    constexpr int padding = AudioPage::padding;
    constexpr int rowHeight = AudioPage::rowHeight;
    constexpr int labelWidth = AudioPage::labelWidth;
    constexpr int valueWidth = AudioPage::comboWidth;

    int y = padding;
    int x = padding;

    auto layoutRow = [&](juce::Label& label, juce::Component& value, int valueW) {
        label.setBounds(x, y, labelWidth, rowHeight);
        value.setBounds(x + labelWidth + 8, y + (rowHeight - 24) / 2, valueW, 24);
        y += rowHeight;
    };

    layoutRow(versionLabel,   versionValue,   valueWidth);
    layoutRow(commitLabel,    commitValue,    valueWidth);

    // Install ID row — shorter value field to make room for Copy button.
    installIdLabel.setBounds(x, y, labelWidth, rowHeight);
    int copyBtnW = 60;
    installIdValue.setBounds(x + labelWidth + 8, y + (rowHeight - 24) / 2,
                              valueWidth - copyBtnW - 8, 24);
    copyIdButton.setBounds(x + labelWidth + 8 + valueWidth - copyBtnW,
                            y + (rowHeight - 24) / 2, copyBtnW, 24);
    y += rowHeight;

    layoutRow(firstSeenLabel, firstSeenValue, valueWidth);

    // Diagnostics toggle — label + toggle stretched along row
    diagnosticsLabel.setBounds(x, y, labelWidth, rowHeight);
    diagnosticsToggle.setBounds(x + labelWidth + 8, y, valueWidth, rowHeight);
    y += rowHeight;

    // Open-last-project toggle
    restoreLastProjectLabel.setBounds(x, y, labelWidth, rowHeight);
    restoreLastProjectToggle.setBounds(x + labelWidth + 8, y, valueWidth, rowHeight);
}


// --- PluginsPage ---

SettingsWindow::PluginsPage::PluginsPage(EngineAPI* engineIn,
                                          std::function<void()> syncCatalogIn)
    : engine(engineIn), syncPluginCatalog(std::move(syncCatalogIn)) {
    buildEntries();
    refreshInstallState();

    list = std::make_unique<List>(entries, installedArchives,
        [this](const juce::String& slug) { triggerRemoveArchive(slug); });

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(list.get(), false);
    viewport.setScrollBarsShown(true, false);

    addAndMakeVisible(actionButton);
    actionButton.onClick = [this]() {
        if (installedArchives.empty()) triggerInstall();
        else                           triggerRemoveAll();
    };
}

void SettingsWindow::PluginsPage::buildEntries() {
    juce::String manifestText(juce::String::createStringFromData(
        BinaryData::manifest_json, BinaryData::manifest_jsonSize));
    auto parsed = juce::JSON::parse(manifestText);
    auto* arr = parsed.getProperty("plugins", juce::var()).getArray();
    if (!arr) return;
    for (auto& v : *arr) {
        Entry e;
        e.name        = v.getProperty("name", "").toString();
        e.category    = v.getProperty("category", "").toString();
        e.description = v.getProperty("description", "").toString();
        e.license     = v.getProperty("license", "").toString();
        e.sourceUrl   = v.getProperty("sourceUrl", "").toString();
        e.archive     = v.getProperty("archive", "").toString();
        entries.push_back(std::move(e));
    }
}

void SettingsWindow::PluginsPage::refreshInstallState() {
    installedArchives.clear();
    for (auto& a : BundledPluginInstaller::readInstalledManifest()) {
        installedArchives.insert(juce::String(a.slug));
    }
    actionButton.setButtonText(installedArchives.empty()
        ? juce::String::fromUTF8("Install plugin pack\xe2\x80\xa6")
        : juce::String::fromUTF8("Remove all plugins\xe2\x80\xa6"));
    if (auto* l = list.get()) l->repaint();
    repaint();
}

void SettingsWindow::PluginsPage::triggerInstall() {
    auto* eng = engine;
    auto sync = syncPluginCatalog;
    PluginInstallDialog::show([eng, sync, this]() {
        if (eng) eng->rescanPlugins();
        juce::MessageManager::callAsync([sync, this]() {
            if (sync) sync();
            refreshInstallState();
        });
    });
}

void SettingsWindow::PluginsPage::triggerRemoveAll() {
    int total = 0;
    for (auto& a : BundledPluginInstaller::readInstalledManifest()) {
        total += (int) a.installedPaths.size();
    }
    auto opts = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Remove all bundled plugins?")
        .withMessage("This will delete " + juce::String(total)
                     + juce::String::fromUTF8(" AU plugin bundles from\n")
                     + BundledPluginInstaller::componentsDirectory().getFullPathName()
                     + ".\n\nYou can reinstall anytime from Help.")
        .withButton("Remove all")
        .withButton("Cancel");
    juce::AlertWindow::showAsync(opts, [this](int r) {
        if (r != 1) return;
        int removed = BundledPluginInstaller::uninstallAll();
        perfLog("[PluginInstall] removed all (%d bundles)\n", removed);
        if (engine) engine->pruneMissingPlugins();
        if (syncPluginCatalog) syncPluginCatalog();
        refreshInstallState();
    });
}

void SettingsWindow::PluginsPage::triggerRemoveArchive(const juce::String& slug) {
    // Find the archive's plugin names for the confirmation dialog.
    juce::StringArray affected;
    for (auto& e : entries) {
        if (e.archive == slug) affected.add(e.name);
    }
    auto opts = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle(juce::String::fromUTF8("Remove ") + slug + "?")
        .withMessage(juce::String("This will remove ")
                     + juce::String(affected.size()) + " plugin"
                     + (affected.size() == 1 ? "" : "s") + ":\n\n"
                     + affected.joinIntoString("\n")
                     + "\n\nYou can reinstall anytime from Help.")
        .withButton("Remove")
        .withButton("Cancel");
    juce::AlertWindow::showAsync(opts, [this, slug](int r) {
        if (r != 1) return;
        int removed = BundledPluginInstaller::uninstallArchive(slug.toStdString());
        perfLog("[PluginInstall] removed %s (%d bundles)\n", slug.toRawUTF8(), removed);
        if (engine) engine->pruneMissingPlugins();
        if (syncPluginCatalog) syncPluginCatalog();
        refreshInstallState();
    });
}

void SettingsWindow::PluginsPage::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(40).reduced(16, 6);

    // Reserve right side for the action button (resized() places it).
    auto headerText = header.withTrimmedRight(180);
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeLg));
    g.drawText(juce::String((int)entries.size()) + " free plugins bundled with Performance",
               headerText, juce::Justification::centredLeft);

    auto subtitle = bounds.removeFromTop(22).reduced(16, 0);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeSm));
    int installedCount = 0;
    for (auto& e : entries)
        if (installedArchives.count(e.archive)) ++installedCount;
    g.drawText(juce::String(installedCount) + " of " + juce::String(entries.size())
                   + " installed",
               subtitle, juce::Justification::centredLeft);
}

void SettingsWindow::PluginsPage::resized() {
    auto bounds = getLocalBounds();
    auto headerRow = bounds.removeFromTop(40).reduced(16, 6);
    auto btn = headerRow.removeFromRight(170).reduced(0, 0);
    actionButton.setBounds(btn);
    bounds.removeFromTop(22);  // subtitle
    bounds.reduce(12, 8);

    viewport.setBounds(bounds);
    int listWidth = bounds.getWidth() - 12;
    if (list) list->setSize(listWidth, list->desiredHeight());
}

// ----- List (rows) -----

juce::Rectangle<int> SettingsWindow::PluginsPage::List::removeButtonBounds(int rowIndex) const {
    // Button sits flush to the right edge of the row, centered vertically.
    int btnW = 70, btnH = 22;
    int y = rowIndex * rowHeight + (rowHeight - btnH) / 2;
    return { getWidth() - btnW - 12, y, btnW, btnH };
}

void SettingsWindow::PluginsPage::List::paint(juce::Graphics& g) {
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& e = entries[i];
        auto row = juce::Rectangle<int>(0, (int)i * rowHeight, getWidth(), rowHeight);

        // Alternating row background for readability.
        g.setColour(i % 2 == 0 ? Theme::color(Theme::Color::bgSurface)
                                : Theme::color(Theme::Color::bgStripe));
        g.fillRect(row);

        bool installed = installedArchives.count(e.archive) > 0;

        // Leave room on the right for either the install badge (when
        // installed, no button) or a Remove button (when installed and
        // this row is hovered). Fixed 80px reserve keeps the layout
        // stable either way.
        auto inner = row.reduced(12, 6).withTrimmedRight(82);

        // Row 1: name (big) + category/license (small, right of inner).
        auto row1 = inner.removeFromTop(22);
        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(Theme::fontSizeLg));
        g.drawText(e.name, row1, juce::Justification::centredLeft);

        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(e.category + "  \xc2\xb7  " + e.license, row1,
                   juce::Justification::centredRight);

        // Row 2: description.
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(e.description, inner, juce::Justification::centredLeft);

        // Right edge: either a Remove button (on hover, if installed)
        // or a small "Installed" text label (no hover, if installed).
        if (installed) {
            if ((int) i == hoverRow) {
                auto btn = removeButtonBounds((int) i);
                g.setColour(Theme::color(Theme::Color::bgControl));
                g.fillRoundedRectangle(btn.toFloat(), 4.0f);
                g.setColour(Theme::color(Theme::Color::border));
                g.drawRoundedRectangle(btn.toFloat(), 4.0f, 1.0f);
                g.setColour(Theme::color(Theme::Color::textPrimary));
                g.setFont(Theme::font(Theme::fontSizeSm));
                g.drawText("Remove", btn, juce::Justification::centred);
            } else {
                auto badge = juce::Rectangle<int>(
                    getWidth() - 82, (int) i * rowHeight, 70, rowHeight);
                g.setColour(Theme::color(Theme::Color::accentDim));
                g.setFont(Theme::font(Theme::fontSizeSm));
                g.drawText("Installed", badge, juce::Justification::centredRight);
            }
        } else {
            auto badge = juce::Rectangle<int>(
                getWidth() - 82, (int) i * rowHeight, 70, rowHeight);
            g.setColour(Theme::color(Theme::Color::textDim));
            g.setFont(Theme::font(Theme::fontSizeSm));
            g.drawText("Not installed", badge, juce::Justification::centredRight);
        }
    }
}

void SettingsWindow::PluginsPage::List::mouseDown(const juce::MouseEvent& e) {
    int idx = e.y / rowHeight;
    if (idx < 0 || idx >= (int) entries.size()) return;
    auto& entry = entries[(size_t) idx];
    if (!installedArchives.count(entry.archive)) return;
    auto btn = removeButtonBounds(idx);
    if (btn.contains(e.getPosition()) && onRemove) {
        onRemove(entry.archive);
    }
}

void SettingsWindow::PluginsPage::List::mouseMove(const juce::MouseEvent& e) {
    int idx = e.y / rowHeight;
    if (idx != hoverRow) {
        hoverRow = idx;
        repaint();
    }
}
