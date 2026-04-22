#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "api/StateAPI.h"
#include "gui/Theme.h"
#include "state/StateModel.h"

// Shared helpers that keep the three track-showing panes (Produce, Mixer,
// Looper) visually and behaviorally consistent.
//
// See docs/LIVE_INPUT_AND_FOCUS.md for the design.
//
// Used by all panes so the "same track always looks the same" rule holds.
// Also the single place that implements the plain/Cmd/Shift click policy,
// so pane click handlers are trivial one-liners.
namespace TrackUi {

// Flat track-row background: one color per state (muted > focused >
// selected > active). Used as the base fill for every pane. Focus
// emphasis is a separate header-region gradient; callers layer it on
// top when appropriate — see paintFocusHeaderGradient.
inline void paintTrackBgFlat(juce::Graphics& g, juce::Rectangle<int> bounds,
                               bool muted, bool focused, bool selected) {
    auto token = muted    ? Theme::Color::bgRowMuted
               : focused  ? Theme::Color::bgRowFocused
               : selected ? Theme::Color::bgRowSelected
                          : Theme::Color::bgRowActive;
    g.setColour(Theme::color(token));
    g.fillRect(bounds);
}

// Convenience: paint the flat row bg for a specific track based on
// current focus/selection state.
inline void paintTrackBgFlatForTrack(juce::Graphics& g, juce::Rectangle<int> bounds,
                                       StateAPI& state, const TrackState& t) {
    bool focused = state.getFocusedTrackId() == t.id;
    auto selected = state.selectedTrackIds();
    bool isSelected = std::find(selected.begin(), selected.end(), t.id) != selected.end();
    paintTrackBgFlat(g, bounds, t.muted, focused, isSelected);
}

// Plain/Cmd/Shift click policy for track rows / mixer strips / looper rows.
// See docs/LIVE_INPUT_AND_FOCUS.md for the full rules.
//
// Plain click   → focus=T, selection=[T], I=T only, I=off on others.
// Cmd-click     → toggle T in selection; focus + I unchanged.
// Shift-click   → range-select from current to T (for now, same as Cmd since
//                 we don't own the range anchor; caller may pass a richer
//                 policy if it has one).
//
// `modifiers` is JUCE's ModifierKeys; commandModifier is ⌘ on macOS, shift
// is ⇧. I-snapping is deferred behind a feature flag until phase 3 engine
// work lands — for now the function sets focus+selection only (plain click
// still reflects user intent correctly; I routing just doesn't do anything
// yet). When phase 3 lands, flip `kSnapInputMonitoring` to true.
static constexpr bool kSnapInputMonitoring = false;

inline void handleTrackClick(StateAPI& state, const TrackId& trackId,
                              const juce::ModifierKeys& modifiers) {
    if (modifiers.isCommandDown()) {
        // Toggle membership; leave focus alone.
        state.selectTrack(trackId, /*addToSelection=*/true);
        return;
    }
    if (modifiers.isShiftDown()) {
        // Range-select is pane-specific (depends on visible ordering).
        // Fall through to the same behavior as Cmd-click for now.
        state.selectTrack(trackId, /*addToSelection=*/true);
        return;
    }
    // Plain click: reset selection to just this track, and move focus here.
    state.clearSelection();
    state.selectTrack(trackId, /*addToSelection=*/false);
    state.setFocusedTrackId(trackId);
    if constexpr (kSnapInputMonitoring) {
        // Phase 3 will enable this: snap I to this-only across all tracks.
        // Left as a no-op today to keep the engine behavior unchanged.
        for (auto& t : state.listTracks()) {
            state.setTrackInputMonitoring(t.id, t.id == trackId);
        }
    }
}

}  // namespace TrackUi
