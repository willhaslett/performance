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
        static constexpr int rowHeight = 40;
        static constexpr int labelWidth = 160;
        static constexpr int comboWidth = 280;
        static constexpr int padding = 20;
    private:
    };

    AudioPage audioPage;

    // Placeholder MIDI page
    struct MidiPage : public juce::Component {
        void paint(juce::Graphics& g) override {
            g.fillAll(Theme::color(Theme::Color::bgApp));
            g.setColour(Theme::color(Theme::Color::textDim));
            g.setFont(Theme::font(14.0f));
            g.drawText("MIDI settings coming soon", getLocalBounds(), juce::Justification::centred);
        }
    };
    MidiPage midiPage;

    // Custom LookAndFeel to remove tab borders
    struct TabLookAndFeel : public juce::LookAndFeel_V4 {
        void drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                            bool isMouseOver, bool isMouseDown) override {
            auto area = button.getLocalBounds();
            bool isFront = button.isFrontTab();
            g.setColour(isFront ? Theme::color(Theme::Color::bgApp)
                                 : Theme::color(Theme::Color::bgPanel));
            g.fillRect(area);
            g.setColour(isFront ? Theme::color(Theme::Color::textOnColor)
                                 : Theme::color(Theme::Color::textPrimary));
            g.setFont(Theme::font(Theme::fontSizeLg));
            g.drawText(button.getButtonText(), area, juce::Justification::centred);
        }
        void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {
            // Don't fill — individual tab buttons handle their own backgrounds
        }
        int getTabButtonOverlap(int) override { return 0; }
    };
    TabLookAndFeel tabLF;

    // Tab bar
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
