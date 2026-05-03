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

    // Bindable performer gesture buttons — one segmented strip in the
    // top bar, in performer order (play | replace overdub | undo redo
    // mute clear | prev next | reset). MIDI binding is done via the
    // Mappings pane (BindableButton's right-click menu also offers it).
    std::unique_ptr<BindableButton> playBtn;
    std::unique_ptr<BindableButton> replaceBtn;
    std::unique_ptr<BindableButton> overdubBtn;
    std::unique_ptr<BindableButton> undoBtn;
    std::unique_ptr<BindableButton> redoBtn;
    std::unique_ptr<BindableButton> muteBtn;
    std::unique_ptr<BindableButton> clearBtn;
    std::unique_ptr<BindableButton> focusPrevBtn;
    std::unique_ptr<BindableButton> focusNextBtn;
    std::unique_ptr<BindableButton> resetBtn;
    void rebuildRowGeoms();

    // ---- Paint helpers ----
    void paintTopBar(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintTrackHeader(juce::Graphics& g, const TrackState& t, const RowGeom& row);
    void paintTrackTimeline(juce::Graphics& g, const TrackState& t, const RowGeom& row);
    void paintLoopNotes(juce::Graphics& g, juce::Rectangle<int> bounds,
                        const RegionState& loop);
    // Lower-level renderer used by both the committed-loop path and the
    // in-flight live-capture path. `openNoteEndBeat` caps any notes
    // that haven't seen a noteOff yet — set to lengthBeats for committed
    // takes (full bar), to the live playhead for in-flight overlay so
    // the in-progress note grows under the playhead instead of jumping
    // to the right edge of the lane.
    // `velocityHueBase` drives the per-note color (velocity scales
    // brightness + alpha); pass typeInstrument for parity with the
    // Producer's MIDI region preview, or triggerLight for the live
    // recording overlay.
    void paintNotes(juce::Graphics& g, juce::Rectangle<int> bounds,
                    const std::vector<MidiEventState>& events,
                    double lengthBeats,
                    double openNoteEndBeat,
                    juce::Colour velocityHueBase);
    // Audio waveform (centered amplitude bars from the take's peak cache).
    void paintAudioWaveform(juce::Graphics& g, juce::Rectangle<int> bounds,
                            const TakeState& take, double lengthBeats);
    // Lower-level waveform renderer used by both the committed take and
    // the live in-flight overlay during an audio capture. `cap` clips
    // the visible peaks horizontally — set to lengthBeats for committed,
    // to the live playhead beat for the in-flight overlay.
    void paintRawWaveform(juce::Graphics& g, juce::Rectangle<int> bounds,
                          const std::vector<std::pair<float, float>>& peaks,
                          int samplesPerPeak, int sampleRate,
                          double recordTempo, double lengthBeats,
                          double cap, juce::Colour baseHue);
    void paintPlayhead(juce::Graphics& g);
    void paintEmptyRow(juce::Graphics& g, juce::Rectangle<int> bounds);

    // ---- Interaction ----

    // Beat-to-pixel mapping using the current cycle length.
    double cycleBeats() const;
    double beatsToX(double beat, juce::Rectangle<int> timelineBounds) const;
};
