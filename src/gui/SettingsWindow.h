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

    // About page — version, commit, install identity, diagnostics toggle
    class AboutPage : public juce::Component {
    public:
        explicit AboutPage(StateAPI& state);
        void paint(juce::Graphics& g) override;
        void resized() override;
    private:
        StateAPI& state;
        juce::Label versionLabel, commitLabel, installIdLabel, firstSeenLabel, diagnosticsLabel;
        juce::TextEditor versionValue, commitValue, installIdValue, firstSeenValue;
        juce::TextButton copyIdButton { "Copy" };
        juce::ToggleButton diagnosticsToggle;
    };
    AboutPage aboutPage;

    // Plugins page — shows the bundled-plugin pack listed in
    // runtime/bundled-plugins/manifest.json. Read-only for now (step 1 of
    // docs/BUNDLED_PLUGINS.md); per-plugin Remove + "Remove all" land in
    // step 5 once the actual .component bundles ship.
    class PluginsPage : public juce::Component {
    public:
        PluginsPage();
        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        struct Entry {
            juce::String name, category, description, license, sourceUrl;
        };
        std::vector<Entry> entries;
        void buildEntries();

        // Inner list component — paints all rows itself, sized large enough
        // to need scrolling inside the viewport.
        class List : public juce::Component {
        public:
            explicit List(const std::vector<Entry>& e) : entries(e) {}
            void paint(juce::Graphics& g) override;
            static constexpr int rowHeight = 60;
            int desiredHeight() const { return (int)entries.size() * rowHeight; }
        private:
            const std::vector<Entry>& entries;
        };
        List list { entries };
        juce::Viewport viewport;
    };
    PluginsPage pluginsPage;

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
