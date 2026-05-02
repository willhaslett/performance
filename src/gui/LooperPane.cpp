#include "gui/LooperPane.h"
#include "gui/TrackUi.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "state/StateEvents.h"
#include "engine/Log.h"
#include <algorithm>

namespace {
// 4/4 assumption — matches the rest of the codebase (documented in
// docs/LIVE_LOOPING.md). If variable meter ever lands, cycle math
// will need to consult the song's time signature.
constexpr double kBeatsPerBar = 4.0;
}

// Minimum row height that still fits the two-row track header layout
// (top pad + name row + gap + pill row + bottom pad). Same formula
// the Producer uses; both panes share the zoom_track_row_height
// config so their row heights stay in sync.
static int minTrackRowHeight() {
    return 4 + Theme::headerHeight + 4 + Theme::pillSize + 4;
}

LooperPane::LooperPane(StateAPI& s, EngineAPI& e, PerformanceCoordinator& c)
    : state(s), engine(e), coord(c) {
    setOpaque(true);
    // Accept focus so a click steals it from any stray TextEditor
    // (e.g. the toolbar build-info field) — otherwise global
    // shortcuts like spacebar get swallowed upstream.
    setWantsKeyboardFocus(true);
    // Pick up the row-height that Producer and Looper share via config.
    auto trh = state.getConfig("zoom_track_row_height");
    if (!trh.empty()) trackRowHeight = std::max(minTrackRowHeight(), std::stoi(trh));
    stateSubId = state.events().subscribe([this](const StateEvent& ev) {
        if (ev.entity == StateEvent::Config) {
            // Producer (or anything else) changed the row-height config —
            // pick it up and rebuild geometry.
            auto trh = state.getConfig("zoom_track_row_height");
            if (!trh.empty())
                trackRowHeight = std::max(minTrackRowHeight(), std::stoi(trh));
        }
        // Any state mutation can invalidate our rendering (loop
        // created, take swapped, mute toggled, etc.). Defer to the
        // message thread to be safe — events may fire from any thread.
        // Rebuild row geometry too: track add/remove/reorder changes
        // the row set, which paint() depends on.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<LooperPane>(this)]() {
            if (safe) {
                safe->rebuildRowGeoms();
                safe->repaint();
            }
        });
    });
}

LooperPane::~LooperPane() {
    state.events().unsubscribe(stateSubId);
}

void LooperPane::visibilityChanged() {
    if (isVisible()) startTimerHz(30);
    else             stopTimer();
}

void LooperPane::timerCallback() {
    // Playhead + record-state pulse both depend on live transport /
    // record state. Cheap repaint; the state event bus handles
    // everything that's NOT time-driven.
    repaint();
}

// ---- Cycle math -----------------------------------------------------------

double LooperPane::cycleBeats() const {
    double c = state.getCycleLength();
    return c > 0.0 ? c : 4.0 * kBeatsPerBar;  // fallback for empty state
}

double LooperPane::beatsToX(double beat, juce::Rectangle<int> t) const {
    if (t.getWidth() <= 0) return (double) t.getX();
    double frac = beat / cycleBeats();
    return t.getX() + frac * t.getWidth();
}

// ---- Layout ---------------------------------------------------------------

void LooperPane::resized() {
    rebuildRowGeoms();
}

void LooperPane::rebuildRowGeoms() {
    rowGeoms.clear();
    auto* song = state.currentSong();
    if (!song) return;

    int y = topBarHeight;
    for (auto& t : song->tracks) {
        // Action tracks don't loop — filter them out of the looper view.
        if (t.sourceType == TrackSourceType::Action) continue;

        int rowH = trackRowHeight;
        RowGeom g;
        g.trackId = t.id;
        g.rowBounds = { 0, y, getWidth(), rowH };
        g.headerBounds = g.rowBounds.withWidth(headerWidth);
        g.timelineBounds = g.rowBounds.withTrimmedLeft(headerWidth);

        // Header layout mirrors the Producer's two-row track header:
        // name row on top, pill row below, vertically centered. Pills
        // are M/S/I; no R (record-arming has no role in the gesture-
        // driven looper) and no take selector (takes collapsed into
        // per-track undo).
        const int interRowGap = 4;
        int row1H = Theme::headerHeight;
        int contentH = row1H + interRowGap + Theme::pillSize;
        int topPad = std::max(4, (rowH - contentH) / 2);
        int row1Y = g.rowBounds.getY() + topPad;
        int row2Y = row1Y + row1H + interRowGap;
        int pillX = g.headerBounds.getX() + Theme::spacingM;
        g.muteButton  = { pillX, row2Y, Theme::pillSize, Theme::pillSize };
        pillX += Theme::pillSize + Theme::pillGap;
        g.soloButton  = { pillX, row2Y, Theme::pillSize, Theme::pillSize };
        pillX += Theme::pillSize + Theme::pillGap + Theme::pillGroupGap;
        g.inputButton = { pillX, row2Y, Theme::pillSize, Theme::pillSize };

        rowGeoms.push_back(g);
        y += rowH + rowGap;
    }

    // Top-bar controls — cycle length pill on the right, PANIC reset
    // immediately to its left (cycle field stays at the corner since
    // reset is a rare action and shouldn't grab the prime spot).
    cycleLengthField = { getWidth() - 160, 8, 140, 24 };
    resetButton      = { cycleLengthField.getX() - 80, 8, 70, 24 };
}

// ---- Paint ----------------------------------------------------------------

void LooperPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));
    paintTopBar(g, { 0, 0, getWidth(), topBarHeight });

    auto* song = state.currentSong();
    if (!song) return;

    for (auto& row : rowGeoms) {
        auto* t = state.findTrack(row.trackId);
        if (!t) continue;
        paintTrackHeader(g, *t, row);
        paintTrackTimeline(g, *t, row);
    }

    paintPlayhead(g);
}

void LooperPane::paintTopBar(juce::Graphics& g, juce::Rectangle<int> bounds) {
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(bounds);

    // Title on the left.
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeLg));
    g.drawText("Looper", bounds.reduced(Theme::spacingL, 0),
               juce::Justification::centredLeft);

    // (Cycle progress is shown by an in-timeline fill behind the
    // playhead — see paintPlayhead. The top-bar strip is gone.)

    // Cycle-length editor pill on the right. Shows "<N> bars" — or
    // "no cycle" in bootstrap mode (cycleEnd == 0). Click opens picker.
    double actualCyc = state.getCycleLength();
    g.setColour(Theme::color(Theme::Color::bgControl));
    g.fillRoundedRectangle(cycleLengthField.toFloat(), 4.0f);
    g.setColour(Theme::color(actualCyc > 0.0
                                 ? Theme::Color::textPrimary
                                 : Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeMd));
    juce::String label;
    if (actualCyc > 0.0) {
        int bars = (int) std::round(actualCyc / kBeatsPerBar);
        label = "cycle: " + juce::String(bars) + " bars";
    } else {
        label = "no cycle yet";
    }
    g.drawText(label, cycleLengthField, juce::Justification::centred);

    // PANIC reset button — wipes everything and returns to bootstrap.
    g.setColour(Theme::color(Theme::Color::bgControl));
    g.fillRoundedRectangle(resetButton.toFloat(), 4.0f);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.drawText("reset", resetButton, juce::Justification::centred);
}

void LooperPane::paintTrackHeader(juce::Graphics& g, const TrackState& t,
                                    const RowGeom& row) {
    // Row background — one color for the whole row (header + timeline),
    // driven by the shared TrackUi rule so Produce / Mixer / Looper all
    // shade the same track identically.
    // Flat row bg (same rule as Produce/Mixer).
    TrackUi::paintTrackBgFlatForTrack(g, row.rowBounds, state, t);

    // Type accent — left-edge stripe. Thickens on focus as the "this is
    // the track I'm playing into" affordance, matching ProducePane.
    // Action tracks are filtered out upstream and don't appear here.
    if (t.sourceType == TrackSourceType::Instrument
        || t.sourceType == TrackSourceType::AudioInput) {
        bool focused = state.getFocusedTrackId() == t.id;
        int thickness = focused ? 4 : 3;
        g.setColour(Theme::color(t.sourceType == TrackSourceType::Instrument
                                   ? Theme::Color::typeInstrument
                                   : Theme::Color::typeAudio));
        g.fillRect(row.rowBounds.getX(), row.rowBounds.getY(),
                   thickness, row.rowBounds.getHeight());
    }

    // Divider between header and timeline.
    g.setColour(Theme::color(Theme::Color::borderSubtle));
    g.fillRect(headerWidth - 1, row.rowBounds.getY(),
               1, row.rowBounds.getHeight());

    // Two-row layout: name on top, pills below.
    int row1Y = row.muteButton.getY() - Theme::headerHeight - 4;
    int nameX = row.headerBounds.getX() + Theme::spacingM;
    auto nameArea = juce::Rectangle<int>(nameX, row1Y,
                                          row.headerBounds.getRight() - nameX - 4,
                                          Theme::headerHeight);
    g.setColour(Theme::color(t.muted ? Theme::Color::textDim
                                      : Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeLg));
    g.drawText(t.name, nameArea, juce::Justification::centredLeft);

    // Pill drawing helper — same style across all looper pills.
    auto drawPill = [&](juce::Rectangle<int> bounds, const char* label,
                         bool active, uint32_t activeColor) {
        g.setColour(active ? Theme::color(activeColor)
                            : Theme::color(Theme::Color::bgControl));
        g.fillRoundedRectangle(bounds.toFloat(), Theme::pillRadius);
        g.setColour(active ? Theme::color(Theme::Color::textOnColor)
                            : Theme::color(Theme::Color::pillTextOff));
        g.setFont(Theme::font(Theme::fontSizePill));
        g.drawText(label, bounds, juce::Justification::centred);
    };

    drawPill(row.muteButton, "M", t.muted, Theme::Color::pillMute);
    drawPill(row.soloButton, "S", t.soloed, Theme::Color::pillSolo);
    if (t.sourceType == TrackSourceType::Instrument
        || t.sourceType == TrackSourceType::AudioInput)
        drawPill(row.inputButton, "I", t.inputMonitoring, Theme::Color::pillInput);

    // Gesture-state badge — visible when this track has a queued or
    // in-flight looper action. Anchored next to the pills on the same
    // row so it doesn't fight the name above.
    auto loopAct = state.getLoopAction(t.id);
    if (loopAct != LoopAction::None) {
        const char* label = "";
        bool capturing = false;
        switch (loopAct) {
            case LoopAction::ReplaceQueued:    label = "REPLACE QUEUED";   break;
            case LoopAction::OverdubQueued:    label = "OVERDUB QUEUED";   break;
            case LoopAction::CapturingReplace: label = "REPLACING…";  capturing = true; break;
            case LoopAction::CapturingOverdub: label = "OVERDUBBING…"; capturing = true; break;
            default: break;
        }
        int badgeX = row.inputButton.getRight() + Theme::spacingM;
        auto badgeArea = juce::Rectangle<int>(badgeX, row.muteButton.getY(),
                                                row.headerBounds.getRight() - badgeX - 4,
                                                Theme::pillSize);
        g.setColour(capturing ? Theme::color(Theme::Color::accent)
                              : Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(Theme::fontSizeXs));
        g.drawText(label, badgeArea, juce::Justification::centredLeft);
    }
}

void LooperPane::paintTrackTimeline(juce::Graphics& g, const TrackState& t,
                                      const RowGeom& row) {
    // No separate background — the row bg drawn by paintTrackHeader
    // already covers this area. Header and timeline read as one unit
    // so focus/selection shading applies uniformly across the row.
    (void) t;
    (void) row;

    if (t.loops.empty()) {
        paintEmptyRow(g, row.timelineBounds);
        return;
    }
    auto& loop = t.loops[0];

    // Render the loop's content repeated across the cycle per the
    // playback rule. If the loop is longer than the cycle, the tail
    // past cycleBeats gets a faded overlay.
    double cyc = cycleBeats();
    double loopLen = loop.lengthBeats;
    if (loopLen <= 0.0) {
        paintEmptyRow(g, row.timelineBounds);
        return;
    }

    int reps = (int) std::ceil(cyc / loopLen);
    for (int rep = 0; rep < reps; ++rep) {
        double repStart = rep * loopLen;
        double repEnd   = std::min(repStart + loopLen, cyc);
        if (repStart >= cyc) break;

        int x0 = (int) std::round(beatsToX(repStart, row.timelineBounds));
        int x1 = (int) std::round(beatsToX(repEnd,   row.timelineBounds));
        juce::Rectangle<int> repBounds { x0, row.timelineBounds.getY() + 4,
                                           std::max(1, x1 - x0),
                                           row.timelineBounds.getHeight() - 8 };

        // Region shell — accent-colored for track type (kept simple:
        // instrument = typeInstrument, audio = typeAudio).
        auto col = (t.sourceType == TrackSourceType::AudioInput)
                    ? Theme::color(Theme::Color::typeAudio)
                    : Theme::color(Theme::Color::typeInstrument);
        g.setColour(col.withAlpha(0.25f));
        g.fillRoundedRectangle(repBounds.toFloat(), 3.0f);

        paintLoopNotes(g, repBounds, loop);

        // Fade tail: if this repetition is only partial (loopLen >
        // remaining cycle), stripe it to show "preserved but silent."
        if (repEnd < repStart + loopLen) {
            // Faded overlay for the clipped portion of the region.
            // (We don't actually render beyond the cycle edge here
            // because repBounds already stops at cyc; this is a future
            // hook if we want to visualize the tail.)
        }
    }
}

void LooperPane::paintLoopNotes(juce::Graphics& g, juce::Rectangle<int> bounds,
                                  const RegionState& loop) {
    auto* take = loop.activeTake();
    if (!take || take->events.empty()) return;

    // Simple piano-roll-ish render: find pitch range, map beats to x,
    // pitches to y. Keeps the pane understandable without importing
    // ProducePane's full-fidelity renderer.
    int minPitch = 127, maxPitch = 0;
    for (auto& e : take->events) {
        if ((e.status & 0xF0) != 0x90 || e.data2 == 0) continue;
        if (e.data1 < minPitch) minPitch = e.data1;
        if (e.data1 > maxPitch) maxPitch = e.data1;
    }
    if (minPitch > maxPitch) return;
    int pitchRange = std::max(1, maxPitch - minPitch);

    struct Note { double startBeat, endBeat; int pitch; };
    std::vector<Note> notes;
    std::map<int, double> open;
    for (auto& e : take->events) {
        if ((e.status & 0xF0) == 0x90 && e.data2 > 0) {
            open[e.data1] = e.beatOffset;
        } else if ((e.status & 0xF0) == 0x80
                   || ((e.status & 0xF0) == 0x90 && e.data2 == 0)) {
            auto it = open.find(e.data1);
            if (it != open.end()) {
                notes.push_back({ it->second, e.beatOffset, e.data1 });
                open.erase(it);
            }
        }
    }
    for (auto& [pitch, onBeat] : open) {
        notes.push_back({ onBeat, loop.lengthBeats, pitch });
    }

    g.setColour(Theme::color(Theme::Color::textPrimary).withAlpha(0.85f));
    for (auto& n : notes) {
        double xFrac0 = n.startBeat / loop.lengthBeats;
        double xFrac1 = n.endBeat   / loop.lengthBeats;
        int x0 = bounds.getX() + (int)(xFrac0 * bounds.getWidth());
        int x1 = bounds.getX() + (int)(xFrac1 * bounds.getWidth());
        double yFrac = 1.0 - ((double)(n.pitch - minPitch) / pitchRange);
        int y = bounds.getY() + (int)(yFrac * (bounds.getHeight() - 4)) + 2;
        g.fillRect(x0, y, std::max(2, x1 - x0), 2);
    }
}

void LooperPane::paintEmptyRow(juce::Graphics& g, juce::Rectangle<int> bounds) {
    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(Theme::fontSizeSm));
    g.drawText(juce::String::fromUTF8("no loop \xe2\x80\x94 focus this track, press Record to capture"),
               bounds, juce::Justification::centred);
}

void LooperPane::paintPlayhead(juce::Graphics& g) {
    if (!sequencer || !sequencer->isLoopEnabled()) return;
    double cyc = cycleBeats();
    if (cyc <= 0.0) return;

    double beat = sequencer->getBeatPosition();
    double pos = std::fmod(beat - sequencer->getLoopStart(), cyc);
    if (pos < 0) pos += cyc;

    if (rowGeoms.empty()) return;
    int topY = rowGeoms.front().rowBounds.getY();
    int botY = rowGeoms.back().rowBounds.getBottom();
    auto timeline = rowGeoms.front().timelineBounds;
    int x = (int) std::round(beatsToX(pos, timeline));

    // Cycle-progress fill: tint the timeline area from beat 0 up to the
    // playhead. Subtle but readable from across a stage. Uses the
    // accent color at a low alpha so it shifts the row background
    // without obscuring the loop content drawn on top.
    int fillX0 = timeline.getX();
    if (x > fillX0) {
        g.setColour(Theme::color(Theme::Color::accent).withAlpha(0.10f));
        g.fillRect(fillX0, topY, x - fillX0, botY - topY);
    }

    // Vertical playhead line, drawn last so it sits on top of the fill.
    g.setColour(Theme::color(Theme::Color::playhead));
    g.fillRect(x, topY, 2, botY - topY);
}

// ---- Interaction ----------------------------------------------------------

void LooperPane::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    auto pos = e.getPosition();

    // Top bar — cycle length edit.
    if (cycleLengthField.contains(pos)) {
        showCycleLengthMenu();
        return;
    }
    // Top bar — PANIC reset. Confirms before wiping.
    if (resetButton.contains(pos)) {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Reset looper session?")
                .withMessage("Stops transport, wipes every track's loop content "
                             "and undo history, and resets the cycle length. "
                             "This is the panic button — there's no undo for the reset itself.")
                .withButton("Reset")
                .withButton("Cancel"),
            [this](int result) {
                if (result == 1) coord.resetLooperSession();
            });
        return;
    }

    for (auto& row : rowGeoms) {
        if (!row.rowBounds.contains(pos)) continue;

        // Record + stop are session-level via the transport bar now
        // Per-track record/arm/take affordances are gone (phase 6
        // gesture model). M/S/I pills + click-to-focus on the row.

        if (row.muteButton.contains(pos)) {
            state.setTrackMuted(row.trackId, !state.isTrackMuted(row.trackId));
            return;
        }
        if (row.soloButton.contains(pos)) {
            state.setTrackSoloed(row.trackId, !state.isTrackSoloed(row.trackId));
            return;
        }
        if (row.inputButton.contains(pos)) {
            auto* t = state.findTrack(row.trackId);
            if (t && (t->sourceType == TrackSourceType::Instrument
                   || t->sourceType == TrackSourceType::AudioInput)) {
                state.setTrackInputMonitoring(row.trackId, !t->inputMonitoring);
            }
            return;
        }
        // Click on the row's background (not on a specific control) —
        // treat as a track-level click for focus + selection. Matches
        // Produce's click-on-track-header behavior.
        TrackUi::handleTrackClick(state, row.trackId, e.mods);
        repaint();
        return;
    }
}

void LooperPane::mouseMove(const juce::MouseEvent&) {
    // TODO: hover states. Minimal for v1.
}

void LooperPane::showCycleLengthMenu() {
    juce::PopupMenu menu;
    int barOptions[] = { 1, 2, 4, 8, 12, 16, 24, 32, 48, 64 };
    int currentBars = (int) std::round(cycleBeats() / kBeatsPerBar);
    for (int opt : barOptions) {
        menu.addItem(opt, juce::String(opt) + " bars",
                     true, opt == currentBars);
    }
    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this](int result) {
            if (result <= 0) return;
            state.setCycleLength(result * kBeatsPerBar);
        });
}

bool LooperPane::handleKey(const juce::KeyPress& key) {
    // Shortcut layer — no hard-coded assumptions about which letters
    // land; these fire only when the Looper pane is visible so they
    // don't conflict with Produce's bindings.
    //
    // Minimal keyset for v1. Expand with user feedback.
    //   r  — toggle record on selected track (TODO: selection concept)
    //   m  — toggle mute on selected track
    //   [  — decrement cycle length by one bar
    //   ]  — increment cycle length by one bar
    if (key.isKeyCode('[')) {
        double cur = cycleBeats();
        int bars = std::max(1, (int) std::round(cur / kBeatsPerBar) - 1);
        state.setCycleLength(bars * kBeatsPerBar);
        return true;
    }
    if (key.isKeyCode(']')) {
        int bars = (int) std::round(cycleBeats() / kBeatsPerBar) + 1;
        state.setCycleLength(bars * kBeatsPerBar);
        return true;
    }
    // Cmd+J / Cmd+K — vertical zoom of track row height. Same shortcut
    // and the same shared config as Producer so adjusting in one mode
    // carries to the other.
    if (key.getModifiers().isCommandDown()) {
        auto c = key.getTextCharacter();
        if (c == 'J' || c == 'j') {
            trackRowHeight = juce::jlimit(minTrackRowHeight(), 200,
                                           (int)(trackRowHeight * 1.3));
            state.setConfig("zoom_track_row_height", std::to_string(trackRowHeight));
            rebuildRowGeoms();
            repaint();
            return true;
        }
        if (c == 'K' || c == 'k') {
            trackRowHeight = juce::jlimit(minTrackRowHeight(), 200,
                                           (int)(trackRowHeight / 1.3));
            state.setConfig("zoom_track_row_height", std::to_string(trackRowHeight));
            rebuildRowGeoms();
            repaint();
            return true;
        }
    }
    return false;
}
