#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Sidebar.h"
#include "gui/MixerView.h"
#include "gui/ChatView.h"
#include "gui/DeviceEditorPane.h"
#include "gui/DebugPane.h"
#include "gui/LogPane.h"
#include "gui/BindingsPane.h"
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
    DeviceEditorPane& getDeviceEditor() { return deviceEditor; }

    // Pane switching: left slot (device editor, debug) and right slot (chat, logs)
    void showLeftPane(juce::Component* pane);
    void showRightPane(juce::Component* pane);

    std::function<void()> onSave;

    // Expose panes for wiring
    DeviceEditorPane deviceEditor;
    DebugPane debugPane;
    BindingsPane bindingsPane;
    ChatView chatView;
    LogPane logPane;

private:
    StateAPI& state;
    EngineAPI& engine;

    Sidebar sidebar;
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
};
