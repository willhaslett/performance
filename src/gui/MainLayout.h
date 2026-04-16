#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Sidebar.h"
#include "gui/MixerView.h"
#include "gui/ChatView.h"
#include "gui/DebugPane.h"
#include "gui/LogPane.h"
#include "gui/ControllersPane.h"
#include "gui/SongMappingsPane.h"
#include "gui/TransportBar.h"
#include "gui/ProducePane.h"
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
    Controllers,
    SongMappings,
    Debug,
    Chat,
    Logs,
    Mixer
};

class MainLayout : public juce::Component, public juce::DragAndDropContainer {
public:
    MainLayout(StateAPI& state, EngineAPI& engine, LuaEngine& lua,
               PerformanceCoordinator& coordinator);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

    bool handleGlobalKey(const juce::KeyPress& key);
    bool handleGlobalKeyUp(const juce::KeyPress& key);

    Sidebar& getSidebar() { return sidebar; }
    MixerView& getMixer() { return mixerView; }

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
    ControllersPane controllersPane;
    SongMappingsPane songMappingsPane;
    ProducePane producePane;
    ChatView chatView;
    LogPane logPane;

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

    // Save/restore pane config
    void savePaneConfig();
    void loadPaneConfig();

    Divider sidebarDivider { Divider::Vertical };
    juce::TextEditor buildInfoField;

    int sidebarWidth = 240;
    int dragStartSidebarWidth = 0;
    static constexpr int toolbarHeight = 32;
    static constexpr int minPaneSize = 100;

    // Overlay
    struct Overlay : public juce::Component {
        juce::String message;
        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xaa000000));
            g.setColour(juce::Colour(0xffcccccc));
            g.setFont(juce::FontOptions(18.0f));
            g.drawText(message, getLocalBounds(), juce::Justification::centred);
        }
    };
    Overlay overlay;

    KeyBindingManager* keyBindingMgr = nullptr;

    // Musical Typing
    MusicalTyping musicalTyping;
    bool musicalTypingActive = false;
    juce::Point<int> musicalTypingLastPos { -1, -1 };
    void toggleMusicalTyping();
};
