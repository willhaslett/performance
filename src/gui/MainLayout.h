#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Sidebar.h"
#include "gui/MixerView.h"

class PerformanceAPI;

// Empty placeholder pane
class Panel3 : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff161616));
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawLine(0.0f, (float)getHeight(), (float)getWidth(), (float)getHeight(), 1.0f);
    }
};

// Top-level layout: toolbar + sidebar + panel3 + mixer
class MainLayout : public juce::Component {
public:
    MainLayout(PerformanceAPI& api);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

    bool handleGlobalKey(const juce::KeyPress& key);

private:
    PerformanceAPI& api;

    Sidebar sidebar;
    Panel3 panel3;
    MixerView mixerView;
    bool sidebarOpen = false;
    bool mixerVisible = true;
    static constexpr int sidebarWidth = 240;
    static constexpr int toolbarHeight = 32;
    static constexpr float mixerRatio = 0.3f;
};
