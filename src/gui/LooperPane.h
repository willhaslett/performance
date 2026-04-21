#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "state/StateModel.h"
#include "daw/SequencerAPI.h"
#include <vector>

class StateAPI;
class EngineAPI;
class PerformanceCoordinator;

// Looper pane — an alternative view onto the project that replaces the
// Producer pane in the main content slot when looper mode is active.
// See docs/LIVE_LOOPING.md.
//
// Design summary:
//   - Horizontal axis = one cycle pass (beats 0 → cycleLength), always
//     filling the pane width. No scroll, no zoom.
//   - Each track gets a row. The row header has record / mute /
//     take-selector affordances; the timeline side shows the loop's
//     content repeated across the cycle (short loop repeats, long
//     loop tail is faded).
//   - Playhead sweeps across all rows together.
//
// The UI is secondary to MIDI bindings — the primary performer
// interface is hardware controls mapped to the same Lua functions this
// pane's click handlers call (toggleLoopRecord, setPendingTake,
// setCycleLength).
class LooperPane : public juce::Component,
                   private juce::Timer {
public:
    LooperPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coord);
    ~LooperPane() override;

    void setSequencer(SequencerAPI* seq) { sequencer = seq; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    bool handleKey(const juce::KeyPress& key);

private:
    StateAPI& state;
    EngineAPI& engine;
    PerformanceCoordinator& coord;
    SequencerAPI* sequencer = nullptr;

    int stateSubId = -1;

    // 30Hz refresh while the pane is visible — covers playhead motion
    // and state indicator pulsing (record button, pending-take pill).
    void timerCallback() override;
    void visibilityChanged() override;

    // ---- Layout ----
    static constexpr int topBarHeight     = 40;
    static constexpr int rowHeight        = 96;
    static constexpr int rowGap           = 2;
    static constexpr int headerWidth      = 200;
    static constexpr int recButtonSize    = 28;
    static constexpr int mutePillWidth    = 28;

    struct RowGeom {
        TrackId trackId;
        juce::Rectangle<int> rowBounds;
        juce::Rectangle<int> headerBounds;
        juce::Rectangle<int> timelineBounds;
        juce::Rectangle<int> recordButton;
        juce::Rectangle<int> muteButton;
        juce::Rectangle<int> takeSelector;   // clickable area for take picker
    };
    std::vector<RowGeom> rowGeoms;
    juce::Rectangle<int> cycleLengthField;   // top-bar control
    void rebuildRowGeoms();

    // ---- Paint helpers ----
    void paintTopBar(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintTrackHeader(juce::Graphics& g, const TrackState& t, const RowGeom& row);
    void paintTrackTimeline(juce::Graphics& g, const TrackState& t, const RowGeom& row);
    void paintLoopNotes(juce::Graphics& g, juce::Rectangle<int> bounds,
                        const RegionState& loop);
    void paintPlayhead(juce::Graphics& g);
    void paintEmptyRow(juce::Graphics& g, juce::Rectangle<int> bounds);

    // ---- Interaction ----
    void showTakeMenu(const TrackId& trackId);
    void showCycleLengthMenu();

    // Beat-to-pixel mapping using the current cycle length.
    double cycleBeats() const;
    double beatsToX(double beat, juce::Rectangle<int> timelineBounds) const;
};
