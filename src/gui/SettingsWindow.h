#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "gui/Theme.h"

class StateAPI;
class EngineAPI;

class SettingsWindow : public juce::DocumentWindow {
public:
    SettingsWindow(StateAPI& state, EngineAPI& engine);

    void closeButtonPressed() override { setVisible(false); }

private:
    // Audio settings page
    class AudioPage : public juce::Component, private juce::Timer {
    public:
        AudioPage(StateAPI& state, EngineAPI& engine);
        void paint(juce::Graphics& g) override;
        void resized() override;
        int getDesiredHeight() const;

    private:
        StateAPI& state;
        EngineAPI& engine;

        juce::Label outputLabel, inputLabel, bufferLabel, sampleRateLabel, latencyLabel;
        juce::ComboBox outputBox, inputBox, bufferBox, sampleRateBox;
        juce::Label latencyValue;

        void refresh();
        void applyOutputDevice();
        void applyInputDevice();
        void applyBufferSize();
        void applySampleRate();
        void timerCallback() override;

    public:
        static constexpr int rowHeight = 32;
        static constexpr int labelWidth = 160;
        static constexpr int comboWidth = 280;
        static constexpr int padding = 20;
    private:
    };

    AudioPage audioPage;

    // Tab bar
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
