#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Sidebar.h"
#include "gui/MixerView.h"
#include "gui/ChatView.h"
#include "gui/DeviceEditorPane.h"
#include "gui/DebugPane.h"
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
    DebugPane& getDebugPane() { return debugPane; }

    void showDeviceEditor();
    void showDebugPane();

    std::function<void()> onSave;

private:
    StateAPI& state;
    EngineAPI& engine;

    Sidebar sidebar;
    ChatView chatView;
    DeviceEditorPane deviceEditor;
    DebugPane debugPane;
    PaneContainer paneContainer;
    MixerView mixerView;

    Divider sidebarDivider { Divider::Vertical };

    bool sidebarOpen = true;
    bool mixerVisible = true;

    int sidebarWidth = 240;
    int dragStartSidebarWidth = 0;
    static constexpr int toolbarHeight = 32;
    static constexpr int minPaneSize = 100;
};
