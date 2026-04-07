#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/TrackStrip.h"
#include "gui/BusStrip.h"
#include "gui/OutputStrip.h"
#include "gui/Theme.h"
#include <memory>
#include <vector>

class StateAPI;
class EngineAPI;

class MixerView : public juce::Component, private juce::Timer {
public:
    MixerView(StateAPI& state, EngineAPI& engine);

    int getDesiredHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildStrips();

    StateAPI& state;
    EngineAPI& engine;
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

    struct TrackInfo { juce::String id; juce::String name; };
    struct BusInfo { juce::String id; juce::String name; };

    std::vector<std::unique_ptr<TrackStrip>> trackStrips;
    std::vector<std::unique_ptr<BusStrip>> busStrips;
    std::vector<TrackInfo> lastTracks;
    std::vector<BusInfo> lastBusses;

    // Fixed output strip on the right
    OutputStrip outputStrip;
};
