#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/InlineEditor.h"
#include "daw/SequencerAPI.h"
#include "daw/Arrangement.h"

class StateAPI;

// DAW-style arrange view: transport bar, track headers, timeline grid with regions.

class ProducePane : public juce::Component, public juce::DragAndDropContainer, private juce::Timer {
public:
    ProducePane();
    ~ProducePane() override;

    void setState(StateAPI* state, SequencerAPI* sequencer, Arrangement* arrangement);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    StateAPI* state = nullptr;
    SequencerAPI* sequencer = nullptr;
    Arrangement* arrangement = nullptr;
    int stateSubscriptionId = -1;

    void timerCallback() override;

    // Layout
    static constexpr int transportHeight = 48;
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

    // Click areas — transport buttons
    juce::Rectangle<int> rewindButtonBounds;
    juce::Rectangle<int> stopButtonBounds;
    juce::Rectangle<int> playButtonBounds;
    juce::Rectangle<int> cycleButtonBounds;

    // Per-track power icon bounds (rebuilt each paint)
    std::vector<juce::Rectangle<int>> powerIconBounds;

    // Track drag reordering
    int dragTrackIndex = -1;
    int dragTargetIndex = -1;
    int dragStartY = 0;
    int getTrackIndexAtY(int y) const;

    // Inline name editing
    InlineEditor nameEditor;
    void paintPowerIcon(juce::Graphics& g, juce::Rectangle<int> iconArea, bool enabled);
};
