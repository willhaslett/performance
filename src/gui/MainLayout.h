#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Sidebar.h"
#include "gui/MixerView.h"
#include "gui/ChatView.h"
#include "gui/DebugPane.h"
#include "gui/LogPane.h"
#include "gui/MappingPane.h"
#include "gui/TransportBar.h"
#include "gui/PaneContainer.h"
#include "gui/Divider.h"

class StateAPI;
class EngineAPI;
class LuaEngine;
class PerformanceCoordinator;

class MainLayout : public juce::Component {
public:
    MainLayout(StateAPI& state, EngineAPI& engine, LuaEngine& lua,
               PerformanceCoordinator& coordinator);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

    bool handleGlobalKey(const juce::KeyPress& key);

    Sidebar& getSidebar() { return sidebar; }
    MixerView& getMixer() { return mixerView; }

    // Pane switching
    void showLeftPane(juce::Component* pane);
    void showRightPane(juce::Component* pane);

    std::function<void()> onSave;
    std::function<void()> onOpenSettings;

    // Loading overlay
    void showOverlay(const juce::String& message);
    void hideOverlay();

    // Expose panes for wiring
    DebugPane debugPane;
    MappingPane mappingPane;
    ChatView chatView;
    LogPane logPane;

private:
    StateAPI& state;
    EngineAPI& engine;

    Sidebar sidebar;
    TransportBar transportBar;
    PaneContainer paneContainer;
    MixerView mixerView;

    // Track which pane is active in each slot
    juce::Component* activeLeftPane = nullptr;
    juce::Component* activeRightPane = nullptr;

    Divider sidebarDivider { Divider::Vertical };

    bool sidebarOpen = true;
    bool mixerVisible = true;

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
};
