#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include <vector>
#include <functional>

class StateAPI;

class SendsPanel : public juce::Component {
public:
    SendsPanel(const juce::String& trackId, StateAPI& state);

    struct SendInfo {
        juce::String busName;
        juce::String busId;
        float gain;
        float peakLevel;
    };
    void setSends(const std::vector<SendInfo>& sends);

    struct BusOption {
        juce::String id;
        juce::String name;
    };
    void setAvailableBusses(const std::vector<BusOption>& busOptions);

    int getDesiredHeight() const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    StateAPI& state;
    juce::String trackId;

    struct SendRow {
        juce::String busName;  // display name (empty = unassigned "Send" pill)
        juce::String busId;    // UUID
        float gain = 1.0f;
        float peakLevel = 0.0f;
    };
    std::vector<SendRow> rows;
    std::vector<BusOption> availableBusses;

    static constexpr int rowHeight = 24;
    static constexpr int rowGap = 4;
    static constexpr int knobSize = 18;
    static constexpr int pillKnobGap = 6;
    static constexpr int bottomPadding = 8;

    // Knob drag state
    int draggingRow = -1;
    float dragStartGain = 0.0f;
    int dragStartY = 0;

    juce::Rectangle<int> getPillBounds(int row) const;
    juce::Rectangle<int> getKnobBounds(int row) const;

    void paintKnob(juce::Graphics& g, juce::Rectangle<int> bounds,
                   float gain, float peakLevel) const;
    void showBusPicker(int rowIndex, juce::Point<int> position);

    static juce::Colour vuColor(float peakLevel);
};
