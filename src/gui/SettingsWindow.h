#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "gui/Theme.h"
#include <set>
#include <functional>

class StateAPI;
class EngineAPI;

class SettingsWindow : public juce::DocumentWindow {
public:
    // syncPluginCatalog is called after an install / uninstall from the
    // Plugins tab so the coordinator's plugin catalog re-reads from
    // knownPlugins. Optional — pass {} if not wiring the Plugins tab
    // to a catalog owner.
    SettingsWindow(StateAPI& state, EngineAPI& engine,
                   std::function<void()> syncPluginCatalog = {});

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
        int getDesiredHeight() const;
    private:
        StateAPI& state;
        juce::Label versionLabel, commitLabel, installIdLabel, firstSeenLabel,
                    diagnosticsLabel, restoreLastProjectLabel;
        juce::TextEditor versionValue, commitValue, installIdValue, firstSeenValue;
        juce::TextButton copyIdButton { "Copy" };
        juce::ToggleButton diagnosticsToggle;
        juce::ToggleButton restoreLastProjectToggle;
    };
    AboutPage aboutPage;

    // Plugins page — shows the bundled-plugin pack listed in
    // runtime/bundled-plugins/manifest.json. Header bar has a single
    // action button: "Install plugin pack…" when nothing is installed,
    // "Remove all plugins…" when something is. Per-archive Remove
    // lives in the list rows.
    class PluginsPage : public juce::Component {
    public:
        explicit PluginsPage(EngineAPI* engine = nullptr,
                             std::function<void()> syncPluginCatalog = {});
        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        struct Entry {
            juce::String name, category, description, license, sourceUrl, archive;
        };
        std::vector<Entry> entries;
        void buildEntries();
        void refreshInstallState();
        void triggerInstall();
        void triggerRemoveAll();
        void triggerRemoveArchive(const juce::String& slug);

        EngineAPI* engine = nullptr;
        std::function<void()> syncPluginCatalog;
        std::set<juce::String> installedArchives;  // archive slugs currently installed

        juce::TextButton actionButton;

        // Inner list component — paints all rows itself, sized large enough
        // to need scrolling inside the viewport.
        class List : public juce::Component {
        public:
            List(const std::vector<Entry>& e, const std::set<juce::String>& i,
                 std::function<void(const juce::String&)> onRemoveSlug)
                : entries(e), installedArchives(i), onRemove(std::move(onRemoveSlug)) {}
            void paint(juce::Graphics& g) override;
            void mouseDown(const juce::MouseEvent& e) override;
            void mouseMove(const juce::MouseEvent& e) override;
            static constexpr int rowHeight = 60;
            int desiredHeight() const { return (int)entries.size() * rowHeight; }
        private:
            juce::Rectangle<int> removeButtonBounds(int rowIndex) const;
            const std::vector<Entry>& entries;
            const std::set<juce::String>& installedArchives;
            std::function<void(const juce::String&)> onRemove;
            int hoverRow = -1;
        };
        std::unique_ptr<List> list;
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
