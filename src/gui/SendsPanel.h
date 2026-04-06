#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/FaderMeter.h"
#include <vector>
#include <memory>

class PerformanceAPI;

class SendsPanel : public juce::Component {
public:
    SendsPanel(const juce::String& trackName, PerformanceAPI& api);

    struct SendInfo {
        juce::String busName;
        float gain;
        float peakLevel;
    };
    void setSends(const std::vector<SendInfo>& sends);
    void setAvailableBusses(const std::vector<juce::String>& busNames);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PerformanceAPI& api;
    juce::String trackName;

    struct SendColumn {
        juce::String busName;  // empty = unassigned
        std::unique_ptr<FaderMeter> faderMeter;
    };
    std::vector<SendColumn> columns;
    std::vector<juce::String> availableBusses;

    static constexpr int sendColumnWidth = 40;
    static constexpr int sendHeaderHeight = 22;
    static constexpr int sendPadding = 4;

    void rebuild();
    void showBusPicker(int columnIndex, juce::Point<int> position);

    // Hit areas for send headers
    std::vector<juce::Rectangle<int>> headerBounds;

    void mouseUp(const juce::MouseEvent& event) override;
};
