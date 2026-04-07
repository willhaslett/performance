#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/TrackStrip.h"
#include "gui/BusStrip.h"
#include "gui/OutputStrip.h"
#include "gui/Theme.h"
#include <memory>
#include <vector>

class PerformanceAPI;

class MixerView : public juce::Component, private juce::Timer {
public:
    MixerView(PerformanceAPI& api);

    int getDesiredHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildStrips();

    PerformanceAPI& api;
    int lastDesiredHeight = 0;

    // Scrollable area for tracks + busses
    class StripContainer : public juce::Component {
    public:
        void paint(juce::Graphics& g) override {
            g.fillAll(Theme::color(Theme::Color::bgTrack));
        }
    };
    juce::Viewport viewport;
    StripContainer stripContainer;

    std::vector<std::unique_ptr<TrackStrip>> trackStrips;
    std::vector<std::unique_ptr<BusStrip>> busStrips;
    std::vector<PerformanceAPI::TrackInfo> lastTracks;
    std::vector<PerformanceAPI::BusInfo> lastBusses;

    // Fixed output strip on the right
    OutputStrip outputStrip;
};
