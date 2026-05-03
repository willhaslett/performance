#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "api/StateAPI.h"
#include "gui/Theme.h"
#include "gui/SaveAsDialog.h"
#include "state/ActionRefs.h"
#include "state/StateModel.h"
#include <functional>
#include <vector>

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
// Plain click   → focus=T, selection=[T]. setFocusedTrackId internally
//                 performs the type-aware I snap (instruments only).
// Cmd-click     → toggle T in selection; focus + I unchanged.
// Shift-click   → range-select from current to T (for now, same as Cmd
//                 since we don't own the range anchor; caller may pass
//                 a richer policy if it has one).
//
// `modifiers` is JUCE's ModifierKeys; commandModifier is ⌘ on macOS, shift
// is ⇧.
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
    // setFocusedTrackId handles the type-aware I snap for instruments.
    state.clearSelection();
    state.selectTrack(trackId, /*addToSelection=*/false);
    state.setFocusedTrackId(trackId);
}

// Confirm-on-dependents track removal. Same policy across panes:
// no dependents → remove silently; otherwise show a modal naming
// the dependent action events / bindings before removing them all.
inline void confirmAndRemoveTrack(StateAPI& state, const TrackId& id) {
    auto deps = ActionRefs::countDependents(state, id.str());
    if (deps.actionEvents == 0 && deps.bindings == 0) {
        state.removeTrack(id);
        return;
    }

    auto* trk = state.findTrack(id);
    juce::String name = trk ? juce::String(trk->name) : juce::String("track");

    juce::String parts;
    if (deps.actionEvents > 0)
        parts << deps.actionEvents << " action event" << (deps.actionEvents == 1 ? "" : "s");
    if (deps.bindings > 0) {
        if (parts.isNotEmpty()) parts << " and ";
        parts << deps.bindings << " MIDI binding" << (deps.bindings == 1 ? "" : "s");
    }

    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Delete track",
        "Deleting \"" + name + "\" will also remove " + parts + " that reference it.",
        "Delete", "Cancel", nullptr,
        juce::ModalCallbackFunction::create([&state, id](int ok) {
            if (ok == 1) {
                ActionRefs::removeDependents(state, id.str());
                state.removeTrack(id);
            }
        }));
}

// Bundle of callbacks the track context menu needs. Caller wires these
// from the same coordinator-side functions (saveTrackPreset etc.) that
// the Mixer uses, so the two surfaces always agree on what a "track
// preset" means.
struct TrackMenuCallbacks {
    std::function<void(const juce::String& trackId, const juce::String& presetName)> onSavePreset;
    std::function<void(const juce::String& trackId, const juce::String& presetName)> onLoadPreset;
    std::function<std::vector<juce::String>()> onListPresets;
};

// Right-click context menu for a track header (or mixer strip). Same
// items + behavior across panes — Save Track Preset…, Load Track
// Preset → submenu (only if presets exist), Delete Track. Callbacks
// are passed in so this helper has no coordinator dependency.
inline void showTrackContextMenu(StateAPI& state,
                                   const TrackId& trackId,
                                   const juce::String& trackName,
                                   juce::Point<int> screenPos,
                                   const TrackMenuCallbacks& cb) {
    auto presets = cb.onListPresets ? cb.onListPresets() : std::vector<juce::String>{};

    juce::PopupMenu menu;
    menu.addItem(1, juce::String::fromUTF8("Save Track Preset\xe2\x80\xa6"));

    if (! presets.empty()) {
        juce::PopupMenu loadMenu;
        for (int i = 0; i < (int) presets.size(); ++i)
            loadMenu.addItem(100 + i, presets[i]);
        menu.addSubMenu("Load Track Preset", loadMenu);
    }

    menu.addSeparator();
    menu.addItem(10, "Delete Track");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [&state, trackId, trackName, presets, cb](int result) {
            if (result == 1) {
                juce::StringArray existing;
                for (auto& n : presets) existing.add(n);
                SaveAsDialog::show("Save Track Preset", trackName, existing,
                    [trackId, cb](const juce::String& name) {
                        if (cb.onSavePreset)
                            cb.onSavePreset(juce::String(trackId.str()), name);
                    });
            } else if (result >= 100 && result - 100 < (int) presets.size()) {
                if (cb.onLoadPreset)
                    cb.onLoadPreset(juce::String(trackId.str()), presets[result - 100]);
            } else if (result == 10) {
                confirmAndRemoveTrack(state, trackId);
            }
        });
}

}  // namespace TrackUi
