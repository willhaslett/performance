#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/TrackStrip.h"
#include "gui/Theme.h"
#include <memory>
#include <vector>

class PerformanceAPI;

class MixerView : public juce::Component, private juce::Timer {
public:
    MixerView(PerformanceAPI& api);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildStrips();

    PerformanceAPI& api;
    std::vector<std::unique_ptr<TrackStrip>> strips;
    std::vector<juce::String> lastTrackNames;
};
