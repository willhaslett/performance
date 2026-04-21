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

LooperPane::LooperPane(StateAPI& s, EngineAPI& e, PerformanceCoordinator& c)
    : state(s), engine(e), coord(c) {
    setOpaque(true);
    // Accept focus so a click steals it from any stray TextEditor
    // (e.g. the toolbar build-info field) — otherwise global
    // shortcuts like spacebar get swallowed upstream.
    setWantsKeyboardFocus(true);
    stateSubId = state.events().subscribe([this](const StateEvent&) {
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
    return c > 0.0 ? c : 16.0 * kBeatsPerBar;  // fallback for empty state
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

        RowGeom g;
        g.trackId = t.id;
        g.rowBounds = { 0, y, getWidth(), rowHeight };
        g.headerBounds = g.rowBounds.withWidth(headerWidth);
        g.timelineBounds = g.rowBounds.withTrimmedLeft(headerWidth);

        // Header elements (left to right, top row):
        //   [record btn] [mute pill]        (inside header)
        //   [take selector]                 (second line)
        auto hdr = g.headerBounds.reduced(Theme::spacingM, Theme::spacingS);
        int xCursor = hdr.getX();
        int yTop = hdr.getY();
        g.recordButton = { xCursor, yTop, recButtonSize, recButtonSize };
        xCursor += recButtonSize + Theme::spacingS;
        g.stopButton   = { xCursor, yTop, recButtonSize, recButtonSize };
        xCursor += recButtonSize + Theme::spacingS;
        g.muteButton   = { xCursor, yTop + (recButtonSize - 20) / 2, mutePillWidth, 20 };

        // Take selector — second line, below the buttons. Fills
        // available width in the header minus margins.
        int secondY = yTop + recButtonSize + Theme::spacingS;
        g.takeSelector = { hdr.getX(), secondY,
                           hdr.getWidth(), 22 };

        rowGeoms.push_back(g);
        y += rowHeight + rowGap;
    }

    // Top-bar control — cycle length pill on the right.
    cycleLengthField = { getWidth() - 160, 8, 140, 24 };
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

    // Cycle progress strip under the top bar — thin horizontal bar
    // showing position within the cycle.
    if (sequencer) {
        double cyc = cycleBeats();
        if (cyc > 0.0 && sequencer->isLoopEnabled()) {
            double beat = sequencer->getBeatPosition();
            double pos = std::fmod(beat - sequencer->getLoopStart(), cyc);
            if (pos < 0) pos += cyc;

            // Align the strip with the timeline column — beat 0 is at
            // x = headerWidth, not at x = 0.
            auto strip = juce::Rectangle<int>(headerWidth,
                                               bounds.getBottom() - 2,
                                               bounds.getWidth() - headerWidth, 2);
            g.setColour(Theme::color(Theme::Color::borderSubtle));
            g.fillRect(strip);
            int fillW = (int) (strip.getWidth() * (pos / cyc));
            g.setColour(Theme::color(Theme::Color::accent));
            g.fillRect(strip.withWidth(fillW));
        }
    }

    // Cycle-length editor pill on the right. Shows "<N> bars" — click
    // opens a picker.
    double cyc = cycleBeats();
    int bars = (int) std::round(cyc / kBeatsPerBar);
    g.setColour(Theme::color(Theme::Color::bgControl));
    g.fillRoundedRectangle(cycleLengthField.toFloat(), 4.0f);
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeMd));
    g.drawText("cycle: " + juce::String(bars) + " bars", cycleLengthField,
               juce::Justification::centred);

}

void LooperPane::paintTrackHeader(juce::Graphics& g, const TrackState& t,
                                    const RowGeom& row) {
    // Row background — one color for the whole row (header + timeline),
    // driven by the shared TrackUi rule so Produce / Mixer / Looper all
    // shade the same track identically.
    g.setColour(TrackUi::rowBgForTrack(state, t));
    g.fillRect(row.rowBounds);

    // Divider between header and timeline.
    g.setColour(Theme::color(Theme::Color::borderSubtle));
    g.fillRect(headerWidth - 1, row.rowBounds.getY(),
               1, row.rowBounds.getHeight());

    // Track name, top-right of the mute pill (after record + stop + mute).
    auto nameArea = row.headerBounds.withTrimmedLeft(row.muteButton.getRight()
                                                        + Theme::spacingS)
                                     .withHeight(recButtonSize + 8)
                                     .translated(0, Theme::spacingS);
    g.setColour(Theme::color(t.muted ? Theme::Color::textDim
                                      : Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeMd));
    g.drawText(t.name, nameArea, juce::Justification::centredLeft);

    // Record button — matches Produce's transport record aesthetic.
    auto recState = coord.getLoopRecordState(t.id);
    juce::Colour recColor = Theme::color(Theme::Color::textDim);
    bool recFilled = false;
    if (recState == "armed") {
        recColor = Theme::color(Theme::Color::transportRec);
        // Pulse while waiting for wrap.
        double phase = (juce::Time::getMillisecondCounterHiRes() / 500.0);
        recColor = recColor.withAlpha(0.4f + 0.6f * (float) std::abs(std::sin(phase)));
    } else if (recState == "recording") {
        recColor = Theme::color(Theme::Color::transportRec);
        recFilled = true;
    } else if (recState == "stop-pending") {
        recColor = Theme::color(Theme::Color::transportRec);
        recFilled = true;
        double phase = (juce::Time::getMillisecondCounterHiRes() / 500.0);
        recColor = recColor.withAlpha(0.4f + 0.6f * (float) std::abs(std::sin(phase)));
    }
    auto recBtn = row.recordButton.toFloat();
    if (recFilled) {
        g.setColour(recColor);
        g.fillEllipse(recBtn);
    } else {
        g.setColour(recColor);
        g.drawEllipse(recBtn.reduced(1.0f), 2.0f);
    }

    // Stop button — always a filled square inside a circle. Dim when
    // state is Off, full when there's something to stop.
    bool canStop = (recState != "off");
    auto stopBtn = row.stopButton.toFloat();
    juce::Colour stopColor = Theme::color(canStop ? Theme::Color::textPrimary
                                                  : Theme::Color::textDim);
    g.setColour(stopColor);
    g.drawEllipse(stopBtn.reduced(1.0f), 2.0f);
    // Inner square — two-thirds the ellipse for a classic "stop" glyph.
    auto innerSize = stopBtn.getWidth() * 0.42f;
    auto innerRect = juce::Rectangle<float>(innerSize, innerSize)
                        .withCentre(stopBtn.getCentre());
    g.fillRect(innerRect);

    // Mute pill — same style as Produce's M pill.
    auto muteBtn = row.muteButton.toFloat();
    g.setColour(t.muted ? Theme::color(Theme::Color::pillMute)
                        : Theme::color(Theme::Color::bgControl));
    g.fillRoundedRectangle(muteBtn, 4.0f);
    g.setColour(t.muted ? Theme::color(Theme::Color::textOnColor)
                        : Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(Theme::fontSizeSm));
    g.drawText("M", row.muteButton, juce::Justification::centred);

    // Take selector — shows the active take name or "—" if no loop.
    const RegionState* loop = t.loops.empty() ? nullptr : &t.loops[0];
    juce::String takeLabel;
    if (!loop) {
        takeLabel = "no loop";
    } else {
        const TakeState* active = loop->activeTake();
        juce::String pendingSuffix;
        if (!loop->pendingTakeId.empty() && loop->pendingTakeId != loop->activeTakeId) {
            for (auto& tk : loop->takes) {
                if (tk.id == loop->pendingTakeId) {
                    pendingSuffix = "  → " + juce::String(tk.name);
                    break;
                }
            }
        }
        takeLabel = active ? juce::String(active->name) : juce::String("take");
        takeLabel += "   (" + juce::String((int) loop->takes.size()) + " take"
                    + (loop->takes.size() == 1 ? "" : "s") + ")" + pendingSuffix;
    }
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(row.takeSelector.toFloat(), 4.0f);
    g.setColour(Theme::color(loop ? Theme::Color::textSecondary
                                   : Theme::Color::textDim));
    g.setFont(Theme::font(Theme::fontSizeSm));
    g.drawText(takeLabel,
               row.takeSelector.reduced(Theme::spacingS, 0),
               juce::Justification::centredLeft);
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
    g.drawText("(no loop — tap Rec to capture)", bounds,
               juce::Justification::centred);
}

void LooperPane::paintPlayhead(juce::Graphics& g) {
    if (!sequencer || !sequencer->isLoopEnabled()) return;
    double cyc = cycleBeats();
    if (cyc <= 0.0) return;

    double beat = sequencer->getBeatPosition();
    double pos = std::fmod(beat - sequencer->getLoopStart(), cyc);
    if (pos < 0) pos += cyc;

    // Draw a vertical line spanning all rows at the cycle position.
    if (rowGeoms.empty()) return;
    int topY = rowGeoms.front().rowBounds.getY();
    int botY = rowGeoms.back().rowBounds.getBottom();
    auto timeline = rowGeoms.front().timelineBounds;
    int x = (int) std::round(beatsToX(pos, timeline));

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

    for (auto& row : rowGeoms) {
        if (!row.rowBounds.contains(pos)) continue;

        if (row.recordButton.contains(pos)) {
            coord.toggleLoopRecord(row.trackId);
            repaint();
            return;
        }
        if (row.stopButton.contains(pos)) {
            coord.forceStopLoopRecord(row.trackId);
            repaint();
            return;
        }
        if (row.muteButton.contains(pos)) {
            bool now = !state.isTrackMuted(row.trackId);
            state.setTrackMuted(row.trackId, now);
            return;
        }
        if (row.takeSelector.contains(pos)) {
            showTakeMenu(row.trackId);
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

void LooperPane::showTakeMenu(const TrackId& trackId) {
    auto* t = state.findTrack(trackId);
    if (!t || t->loops.empty()) return;
    auto& loop = t->loops[0];
    if (loop.takes.empty()) return;

    juce::PopupMenu menu;
    for (size_t i = 0; i < loop.takes.size(); ++i) {
        auto& take = loop.takes[i];
        bool isActive = take.id == loop.activeTakeId;
        bool isPending = take.id == loop.pendingTakeId;
        juce::String label = juce::String(take.name);
        if (isActive)  label += "   (current)";
        if (isPending) label += "   (queued)";
        menu.addItem((int) i + 1, label, true, isActive);
    }
    RegionId loopId = loop.id;
    std::vector<TakeId> takeIds;
    for (auto& tk : loop.takes) takeIds.push_back(tk.id);

    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this, loopId, takeIds](int result) {
            if (result <= 0 || result > (int) takeIds.size()) return;
            state.setPendingTake(loopId, takeIds[result - 1]);
        });
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
    return false;
}
