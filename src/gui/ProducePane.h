#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "daw/SequencerAPI.h"
#include "daw/Arrangement.h"

class StateAPI;

// DAW-style arrange view: transport bar, track headers, timeline grid with regions.

class ProducePane : public juce::Component, private juce::Timer {
public:
    ProducePane();

    void setState(StateAPI* state, SequencerAPI* sequencer, Arrangement* arrangement);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

private:
    StateAPI* state = nullptr;
    SequencerAPI* sequencer = nullptr;
    Arrangement* arrangement = nullptr;

    void timerCallback() override;

    // Layout
    static constexpr int transportHeight = 36;
    static constexpr int trackHeaderWidth = 120;
    static constexpr int trackRowHeight = 40;
    static constexpr int rulerHeight = 20;
    static constexpr int beatsPerBar = 4;

    double pixelsPerBeat = 30.0;
    double scrollBeat = 0.0;  // horizontal scroll position in beats
    int scrollTrack = 0;       // vertical scroll (future)

    // Convert beat to x pixel
    int beatToX(double beat) const;
    double xToBeat(int x) const;

    // Paint helpers
    void paintTransport(juce::Graphics& g, juce::Rectangle<int> area);
    void paintRuler(juce::Graphics& g, juce::Rectangle<int> area);
    void paintTrackHeaders(juce::Graphics& g, juce::Rectangle<int> area);
    void paintGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area);

    // Click areas
    juce::Rectangle<int> playButtonBounds;
};
