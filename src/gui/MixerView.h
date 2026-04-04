#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/TrackStrip.h"
#include "gui/Sidebar.h"
#include <memory>
#include <vector>

class PerformanceAPI;

// Main mixer view — sidebar + horizontal row of track strips
class MixerView : public juce::Component, private juce::Timer {
public:
    MixerView(PerformanceAPI& api);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void rebuildStrips();

    PerformanceAPI& api;

    Sidebar sidebar;
    bool sidebarOpen = false;
    static constexpr int sidebarWidth = 240;

    std::vector<std::unique_ptr<TrackStrip>> strips;
    std::vector<juce::String> lastTrackNames;
};
