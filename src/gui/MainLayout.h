#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Sidebar.h"
#include "gui/MixerView.h"
#include "gui/ChatView.h"
#include "gui/Divider.h"

class PerformanceAPI;
class LuaEngine;

class MainLayout : public juce::Component {
public:
    MainLayout(PerformanceAPI& api, LuaEngine& lua);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

    bool handleGlobalKey(const juce::KeyPress& key);

private:
    PerformanceAPI& api;

    Sidebar sidebar;
    ChatView chatView;
    MixerView mixerView;

    Divider sidebarDivider { Divider::Vertical };

    bool sidebarOpen = true;
    bool mixerVisible = true;

    int sidebarWidth = 240;
    int dragStartSidebarWidth = 0;
    static constexpr int toolbarHeight = 32;
    static constexpr int minPaneSize = 100;
};
