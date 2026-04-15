#include "gui/SettingsWindow.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "engine/Log.h"

// --- SettingsWindow ---

SettingsWindow::SettingsWindow(StateAPI& state, EngineAPI& engine)
    : DocumentWindow("Settings", Theme::color(Theme::Color::bgPanel),
                      DocumentWindow::closeButton),
      audioPage(state, engine) {

    tabs.setLookAndFeel(&tabLF);
    tabs.addTab("Audio", Theme::color(Theme::Color::bgApp), &audioPage, false);
    tabs.addTab("MIDI", Theme::color(Theme::Color::bgApp), &midiPage, false);
    tabs.setTabBarDepth(36);
    tabs.setColour(juce::TabbedComponent::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    tabs.setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);

    setContentNonOwned(&tabs, false);
    setUsingNativeTitleBar(true);
    setResizable(false, false);

    int w = AudioPage::labelWidth + AudioPage::comboWidth + AudioPage::padding * 3;
    int h = audioPage.getDesiredHeight() + tabs.getTabBarDepth() + getTitleBarHeight();
    centreWithSize(w, h);
}

// --- AudioPage ---

SettingsWindow::AudioPage::AudioPage(StateAPI& state, EngineAPI& engine)
    : state(state), engine(engine) {

    auto setupLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(Theme::font(Theme::fontSize));
        label.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textSecondary));
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
    };

    auto setupCombo = [this](juce::ComboBox& box) {
        box.setColour(juce::ComboBox::backgroundColourId, Theme::color(Theme::Color::bgSurface));
        box.setColour(juce::ComboBox::textColourId, Theme::color(Theme::Color::textPrimary));
        box.setColour(juce::ComboBox::outlineColourId, Theme::color(Theme::Color::border));
        addAndMakeVisible(box);
    };

    setupLabel(outputLabel, "Output Device:");
    setupLabel(inputLabel, "Input Device:");
    setupLabel(bufferLabel, "I/O Buffer Size:");
    setupLabel(sampleRateLabel, "Sample Rate:");
    setupLabel(latencyLabel, "Resulting Latency:");

    latencyValue.setFont(Theme::font(Theme::fontSize));
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

        // Sample rates
        sampleRateBox.clear(juce::dontSendNotification);
        auto rates = device->getAvailableSampleRates();
        double currentRate = device->getCurrentSampleRate();
        for (int i = 0; i < rates.size(); ++i) {
            sampleRateBox.addItem(juce::String((int)rates[i]) + " Hz", i + 1);
            if (std::abs(rates[i] - currentRate) < 1.0)
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
    setup.bufferSize = sizes[idx];
    state.setConfig("audio_buffer_size", std::to_string(sizes[idx]));
    dm.setAudioDeviceSetup(setup, true);
    refresh();
}

void SettingsWindow::AudioPage::applySampleRate() {
    auto& dm = engine.getDeviceManager();
    auto* device = dm.getCurrentAudioDevice();
    if (!device) return;
    auto rates = device->getAvailableSampleRates();
    int idx = sampleRateBox.getSelectedItemIndex();
    if (idx < 0 || idx >= rates.size()) return;
    auto setup = dm.getAudioDeviceSetup();
    setup.sampleRate = rates[idx];
    state.setConfig("audio_sample_rate", std::to_string((int)rates[idx]));
    dm.setAudioDeviceSetup(setup, true);
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
