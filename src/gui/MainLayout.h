#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Sidebar.h"
#include "gui/MixerView.h"
#include "gui/ChatView.h"
#include "gui/DebugPane.h"
#include "gui/LogPane.h"
#include "gui/MidiMonitorPane.h"
#include "gui/PerformPane.h"
#include "gui/TransportBar.h"
#include "gui/ProducePane.h"
#include "gui/LooperPane.h"
#include "gui/Divider.h"
#include "gui/MusicalTyping.h"
#include "gui/KeyBindingManager.h"
#include <map>
#include <string>

class StateAPI;
class EngineAPI;
class LuaEngine;
class PerformanceCoordinator;

// Pane slots — the four areas of the layout
enum class PaneSlot { Sidebar, Left, Right, Bottom };

// Pane content types
enum class PaneContent {
    Hidden,
    SidebarTree,
    Produce,
    Looper,
    Perform,
    Debug,
    Chat,
    Logs,
    Mixer,
    MidiMonitor
};

class MainLayout : public juce::Component, public juce::DragAndDropContainer {
public:
    MainLayout(StateAPI& state, EngineAPI& engine, LuaEngine& lua,
               PerformanceCoordinator& coordinator);
    ~MainLayout() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    bool handleGlobalKey(const juce::KeyPress& key);
    bool handleGlobalKeyUp(const juce::KeyPress& key);

    Sidebar& getSidebar() { return sidebar; }
    MixerView& getMixer() { return mixerView; }
    ProducePane& getProducer() { return producePane; }

    // Pane assignment — the core abstraction
    void setPaneContent(PaneSlot slot, PaneContent content);
    PaneContent getPaneContent(PaneSlot slot) const;

    // Build a popup menu for a specific slot
    juce::PopupMenu buildPaneMenu(PaneSlot slot);

    std::function<void()> onSave;
    std::function<void()> onOpenSettings;
    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void()> onNewSong;
    std::function<void()> onNewInstrumentTrack;
    std::function<void()> onNewAudioTrack;
    std::function<void()> onNewBus;

    void setKeyBindingManager(KeyBindingManager* mgr) { keyBindingMgr = mgr; }

    // Loading overlay
    void showOverlay(const juce::String& message);
    void hideOverlay();

    // Expose panes for wiring
    DebugPane debugPane;
    PerformPane performPane;
    ProducePane producePane;
    LooperPane looperPane;
    ChatView chatView;
    LogPane logPane;
    MidiMonitorPane midiMonitorPane;

private:
    StateAPI& state;
    EngineAPI& engine;

    Sidebar sidebar;
    TransportBar transportBar;
    MixerView mixerView;

    // Pane assignments — source of truth
    std::map<PaneSlot, PaneContent> paneAssignments;

    // Resolve content enum to component pointer
    juce::Component* componentForContent(PaneContent content);
    static std::string contentToString(PaneContent content);
    static PaneContent stringToContent(const std::string& s);
    static const char* contentLabel(PaneContent content);

    // Save/restore pane config (delegates to mode layouts)
    void savePaneConfig();
    void loadPaneConfig();

    Divider sidebarDivider { Divider::Vertical };
    Divider centerDivider  { Divider::Vertical };  // between Left and Right slots
    juce::TextEditor buildInfoField;

    int sidebarWidth = 240;
    int dragStartSidebarWidth = 0;
    // Override for the Left/Right split. -1 means "use the per-content
    // preferredLeftProportion." Set when the user drags centerDivider;
    // persisted in config so it survives relaunch.
    int leftPaneWidthOverride = -1;
    int dragStartLeftPaneWidth = 0;
    int stateSubId = -1;   // Song Updated reverse-sync (see docs/PANE_MODE_MODEL.md)
    static constexpr int minPaneSize = 100;

    // Loading overlay
    struct Overlay : public juce::Component {
        juce::String message;
        void paint(juce::Graphics& g) override {
            g.fillAll(Theme::color(Theme::Color::overlayDim));
            g.setColour(Theme::color(Theme::Color::textPrimary));
            g.setFont(Theme::font(Theme::fontSizeTitle));
            g.drawText(message, getLocalBounds(), juce::Justification::centred);
        }
    };
    Overlay overlay;

    // Startup song chooser — shown when 1+ songs exist and no song is loaded
    struct StartupChooser : public juce::Component {
        struct SongItem { std::string id; juce::String name; juce::Rectangle<int> bounds; };
        std::vector<SongItem> songs;
        juce::Rectangle<int> newSongBounds;
        int hoveredIndex = -1;  // -1 = none, -2 = new song button, 0+ = song index

        std::function<void(const std::string& songId)> onLoadSong;
        std::function<void()> onNewSong;

        void setSongs(const std::vector<std::pair<std::string, std::string>>& songList);
        void paint(juce::Graphics& g) override;
        void mouseUp(const juce::MouseEvent& event) override;
        void mouseMove(const juce::MouseEvent& event) override;
        void resized() override;
    };
public:
    StartupChooser startupChooser;
private:

    KeyBindingManager* keyBindingMgr = nullptr;

    // Musical Typing
    MusicalTyping musicalTyping;
    bool musicalTypingActive = false;
    juce::Point<int> musicalTypingLastPos { -1, -1 };
    void toggleMusicalTyping();
};
