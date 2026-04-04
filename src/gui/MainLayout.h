#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Sidebar.h"
#include "gui/MixerView.h"

class PerformanceAPI;

// Top-level layout: sidebar + content pane
class MainLayout : public juce::Component {
public:
    MainLayout(PerformanceAPI& api);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

    // Global key handler
    bool handleGlobalKey(const juce::KeyPress& key);

private:
    PerformanceAPI& api;

    Sidebar sidebar;
    MixerView mixerView;
    bool sidebarOpen = false;
    static constexpr int sidebarWidth = 240;
    static constexpr int toolbarHeight = 32;
};
