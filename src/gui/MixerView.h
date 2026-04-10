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
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // Track reordering — called by TrackStrip on header drag
    void onTrackDragStart(const juce::String& trackId, int stripX);
    void onTrackDragMove(int mouseX);
    void onTrackDragEnd();

private:
    void timerCallback() override;
    void rebuildStrips();

    StateAPI& state;
    EngineAPI& engine;
    int lastDesiredHeight = 0;

public:
    // Track preset callbacks — set by coordinator, applied to new strips
    std::function<void(const juce::String&, const juce::String&)> onSaveTrackPreset;
    std::function<void(const juce::String&, const juce::String&)> onLoadTrackPreset;
    std::function<std::vector<juce::String>()> onListTrackPresets;
private:

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

    // Track reorder drag state
    juce::String dragTrackId;
    int dragIndicatorX = -1;

    // Fixed output strip on the right
    OutputStrip outputStrip;
};
