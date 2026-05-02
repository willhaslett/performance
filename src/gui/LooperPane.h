#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/BindableButton.h"
#include "state/StateModel.h"
#include "daw/SequencerAPI.h"
#include <memory>
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
// pane's click handlers call (setTrackArmed, toggleLoopRecord,
// setPendingTake, setCycleLength).
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
    static constexpr int topBarHeight     = 80;   // accommodates 64px BindableButtons
    static constexpr int rowGap           = 2;
    static constexpr int headerWidth      = 140;  // matches Producer
    static constexpr int mutePillWidth    = 28;
    // Row height is dynamic — shared with Producer via the same
    // zoom_track_row_height config so adjusting in one mode follows
    // through to the other. Cmd+J / Cmd+K resize.
    int trackRowHeight = 72;

    struct RowGeom {
        TrackId trackId;
        juce::Rectangle<int> rowBounds;
        juce::Rectangle<int> headerBounds;
        juce::Rectangle<int> timelineBounds;
        juce::Rectangle<int> muteButton;
        juce::Rectangle<int> soloButton;
        juce::Rectangle<int> inputButton;
    };
    std::vector<RowGeom> rowGeoms;
    juce::Rectangle<int> cycleLengthField;   // top-bar control
    juce::Rectangle<int> resetButton;        // top-bar PANIC reset (kept separate for confirm dialog)
    juce::Rectangle<int> learnPill;          // top-bar MIDI Learn toggle

    // MIDI Learn state — owned by the pane, not the buttons. While
    // `learnMode` is true, bindable cells switch from "click fires" to
    // "click arms the next MIDI event." `armedActionName` is the cell
    // that will receive the next captured event; clicking another cell
    // re-arms to that one (we only ever capture for one action at a
    // time so the user can build a bank by clicking through the strip).
    bool learnMode = false;
    juce::String armedActionName;
    void toggleLearnMode();
    void exitLearnMode();
    void armForLearn(const juce::String& actionName);
    void onLearnCapture(const std::string& type, int channel, int number,
                        const std::string& portName);
    void rearmLearnCapture();

    // Bindable performer gesture buttons (top-bar group). One per
    // looper-relevant action; segmented in sub-groups by visual gap.
    std::unique_ptr<BindableButton> playBtn;
    std::unique_ptr<BindableButton> replaceBtn;
    std::unique_ptr<BindableButton> overdubBtn;
    std::unique_ptr<BindableButton> undoBtn;
    std::unique_ptr<BindableButton> redoBtn;
    std::unique_ptr<BindableButton> muteBtn;
    std::unique_ptr<BindableButton> clearBtn;
    std::unique_ptr<BindableButton> focusPrevBtn;
    std::unique_ptr<BindableButton> focusNextBtn;
    void rebuildRowGeoms();

    // ---- Paint helpers ----
    void paintTopBar(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintTrackHeader(juce::Graphics& g, const TrackState& t, const RowGeom& row);
    void paintTrackTimeline(juce::Graphics& g, const TrackState& t, const RowGeom& row);
    void paintLoopNotes(juce::Graphics& g, juce::Rectangle<int> bounds,
                        const RegionState& loop);
    // Lower-level renderer used by both the committed-loop path and the
    // in-flight live-capture path (overlay during CapturingReplace /
    // CapturingOverdub).
    void paintNotes(juce::Graphics& g, juce::Rectangle<int> bounds,
                    const std::vector<MidiEventState>& events,
                    double lengthBeats,
                    juce::Colour color);
    void paintPlayhead(juce::Graphics& g);
    void paintEmptyRow(juce::Graphics& g, juce::Rectangle<int> bounds);

    // ---- Interaction ----

    // Beat-to-pixel mapping using the current cycle length.
    double cycleBeats() const;
    double beatsToX(double beat, juce::Rectangle<int> timelineBounds) const;
};
