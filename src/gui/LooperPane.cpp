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

    // ---- Performer gesture buttons ----
    // Standalone play. Active = "transport is playing" (icon swap).
    playBtn = std::make_unique<BindableButton>(state, coord, "togglePlay", "",
                                                BindableButton::Variant::IconPlay);
    playBtn->setActivePredicate([this] {
        return sequencer && sequencer->isPlaying();
    });
    addAndMakeVisible(*playBtn);

    // Helpers for predicates that look at the focused track.
    auto hasFocus = [this]() {
        return !state.getFocusedTrackId().empty();
    };
    auto focusedAct = [this]() -> LoopAction {
        auto fid = state.getFocusedTrackId();
        return fid.empty() ? LoopAction::None : state.getLoopAction(fid);
    };

    // Color keys for Replace and Overdub. Match the lane-state tints
    // that show on the focused track when the action is queued or
    // capturing — top-stripe on the button is the cue that links the
    // gesture to the visual on the lane.
    const auto replaceColor = juce::Colour(0xffcc6655);  // warm red-orange (record family)
    const auto overdubColor = juce::Colour(0xff5fb09f);  // cool teal

    // All non-play buttons live in one segmented strip — Left for the
    // first cell, Mid for the rest, Right for the last. The vertical
    // dividers between cells signal cohesion.

    replaceBtn = std::make_unique<BindableButton>(state, coord, "replaceLoop", "replace");
    replaceBtn->setCornerStyle(BindableButton::Left);
    replaceBtn->setEnabledPredicate(hasFocus);
    replaceBtn->setTopColorStripe(replaceColor);
    replaceBtn->setShowRecordDot(true);
    addAndMakeVisible(*replaceBtn);

    overdubBtn = std::make_unique<BindableButton>(state, coord, "overdubLoop", "overdub");
    overdubBtn->setCornerStyle(BindableButton::Mid);
    overdubBtn->setEnabledPredicate(hasFocus);
    overdubBtn->setTopColorStripe(overdubColor);
    overdubBtn->setShowRecordDot(true);
    addAndMakeVisible(*overdubBtn);

    undoBtn = std::make_unique<BindableButton>(state, coord, "undoLoop", "undo");
    undoBtn->setCornerStyle(BindableButton::Mid);
    undoBtn->setEnabledPredicate([this] {
        auto fid = state.getFocusedTrackId();
        return !fid.empty() && state.getLoopUndoDepth(fid) > 0;
    });
    addAndMakeVisible(*undoBtn);

    redoBtn = std::make_unique<BindableButton>(state, coord, "redoLoop", "redo");
    redoBtn->setCornerStyle(BindableButton::Mid);
    redoBtn->setEnabledPredicate([this] {
        auto fid = state.getFocusedTrackId();
        return !fid.empty() && state.getLoopRedoDepth(fid) > 0;
    });
    addAndMakeVisible(*redoBtn);

    muteBtn = std::make_unique<BindableButton>(state, coord, "toggleFocusedMute", "mute");
    muteBtn->setCornerStyle(BindableButton::Mid);
    muteBtn->setActivePredicate([this] {
        auto fid = state.getFocusedTrackId();
        return !fid.empty() && state.isTrackMuted(fid);
    });
    muteBtn->setEnabledPredicate(hasFocus);
    addAndMakeVisible(*muteBtn);

    clearBtn = std::make_unique<BindableButton>(state, coord, "clearLoop", "clear");
    clearBtn->setCornerStyle(BindableButton::Mid);
    clearBtn->setEnabledPredicate(hasFocus);
    addAndMakeVisible(*clearBtn);

    focusPrevBtn = std::make_unique<BindableButton>(state, coord, "focusPrevTrack", "",
                                                     BindableButton::Variant::IconArrowUp);
    focusPrevBtn->setCornerStyle(BindableButton::Mid);
    addAndMakeVisible(*focusPrevBtn);

    focusNextBtn = std::make_unique<BindableButton>(state, coord, "focusNextTrack", "",
                                                     BindableButton::Variant::IconArrowDown);
    focusNextBtn->setCornerStyle(BindableButton::Right);
    addAndMakeVisible(*focusNextBtn);

    // Wire MIDI Learn predicates onto every bindable cell. Each cell
    // asks "are we in learn mode? am I the armed target?" — both answers
    // come from this pane's state. Click-to-arm goes through armForLearn,
    // which kicks off the next-event capture.
    BindableButton* allBtns[] = { playBtn.get(), replaceBtn.get(), overdubBtn.get(),
                                   undoBtn.get(), redoBtn.get(), muteBtn.get(),
                                   clearBtn.get(), focusPrevBtn.get(), focusNextBtn.get() };
    for (auto* btn : allBtns) {
        btn->setLearnPredicate([this] { return learnMode; });
        juce::String name = btn->getActionName();
        btn->setArmedPredicate([this, name] { return learnMode && armedActionName == name; });
        btn->setOnArmRequest([this, name] { armForLearn(name); });
    }

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
    // Top-bar layout (right-to-left): cycle pill at the corner, reset
    // pill next, then the BindableButton group taking the remaining
    // space, then play standalone on the left of the group. Title sits
    // at the very left.
    int topBarMid = topBarHeight / 2;
    int pillH = 24;
    int pillY = topBarMid - pillH / 2;
    cycleLengthField = { getWidth() - 160, pillY, 140, pillH };
    resetButton      = { cycleLengthField.getX() - 80, pillY, 70, pillH };
    learnPill        = { resetButton.getX() - 110, pillY, 100, pillH };

    int bbH = BindableButton::desiredHeight;
    int bbY = topBarMid - bbH / 2;
    const int playWidth      = 56;
    const int playToStripGap = 12;  // breathing room between standalone play and the strip
    const int wideBtn        = 84;  // replace, overdub (longer label, plus record dot)
    const int narrowBtn      = 60;  // undo, redo, mute, clear
    const int iconBtn        = 40;  // focus prev/next

    int stripW = wideBtn * 2 + narrowBtn * 4 + iconBtn * 2;
    int bindablesW = playWidth + playToStripGap + stripW;

    int rightEdge = learnPill.getX() - 16;
    int x = rightEdge - bindablesW;

    playBtn->setBounds(x, bbY, playWidth, bbH);
    x += playWidth + playToStripGap;

    replaceBtn->setBounds(x, bbY, wideBtn, bbH); x += wideBtn;
    overdubBtn->setBounds(x, bbY, wideBtn, bbH); x += wideBtn;
    undoBtn->setBounds(x, bbY, narrowBtn, bbH);  x += narrowBtn;
    redoBtn->setBounds(x, bbY, narrowBtn, bbH);  x += narrowBtn;
    muteBtn->setBounds(x, bbY, narrowBtn, bbH);  x += narrowBtn;
    clearBtn->setBounds(x, bbY, narrowBtn, bbH); x += narrowBtn;
    focusPrevBtn->setBounds(x, bbY, iconBtn, bbH); x += iconBtn;
    focusNextBtn->setBounds(x, bbY, iconBtn, bbH);
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

    // Loop-length readout (right). Shows the master length in seconds
    // — set by the first commit. Reads "—" before the first commit.
    // Display only; no picker (Boss-RC: length is captured, not chosen).
    double actualCyc = state.getCycleLength();
    double bpm = state.getSongTempo();
    double bps = bpm > 0.0 ? bpm / 60.0 : 2.0;
    g.setColour(Theme::color(Theme::Color::bgControl));
    g.fillRoundedRectangle(cycleLengthField.toFloat(), 4.0f);
    g.setColour(Theme::color(actualCyc > 0.0
                                 ? Theme::Color::textPrimary
                                 : Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeMd));
    juce::String label;
    if (actualCyc > 0.0) {
        double seconds = actualCyc / bps;
        label = "loop: " + juce::String(seconds, 2) + " s";
    } else {
        label = juce::String::fromUTF8("loop: \xe2\x80\x94");  // em dash
    }
    g.drawText(label, cycleLengthField, juce::Justification::centred);

    // PANIC reset button — wipes everything and returns to bootstrap.
    g.setColour(Theme::color(Theme::Color::bgControl));
    g.fillRoundedRectangle(resetButton.toFloat(), 4.0f);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.drawText("reset", resetButton, juce::Justification::centred);

    // MIDI Learn toggle pill. On = accent fill ("the strip is now an
    // armable surface"). Off = neutral. The bindable cells switch
    // behavior automatically via the predicates set in the ctor.
    g.setColour(learnMode ? Theme::color(Theme::Color::accent)
                          : Theme::color(Theme::Color::bgControl));
    g.fillRoundedRectangle(learnPill.toFloat(), 4.0f);
    g.setColour(learnMode ? Theme::color(Theme::Color::textOnColor)
                          : Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeMd));
    g.drawText(learnMode ? "learning\xe2\x80\xa6" : "MIDI learn",
               learnPill, juce::Justification::centred);

    // (Gesture buttons are BindableButton child components — they paint
    // themselves when JUCE walks the children. See constructor + the
    // layout block in rebuildRowGeoms.)
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

    // (Gesture state — queued / capturing — is shown as a colored
    // tint on the lane's left-of-playhead area in paintPlayhead, not
    // as a text badge here. The lane shows it bigger and from across
    // a stage, plus the pulse → solid → neutral rhythm matches how
    // the action progresses through its cycle.)
}

void LooperPane::paintTrackTimeline(juce::Graphics& g, const TrackState& t,
                                      const RowGeom& row) {
    // No separate background — the row bg drawn by paintTrackHeader
    // already covers this area. Header and timeline read as one unit
    // so focus/selection shading applies uniformly across the row.
    (void) t;
    (void) row;

    // Pull the in-flight gesture capture (if it targets this track) so
    // the user sees notes appear as they're played, not only after the
    // cycle wraps. Always nullopt for non-instrument targets after the
    // drain-side gate, but checking the type here keeps the lane
    // unambiguous.
    auto inFlight = coord.getInFlightLoopCapture();
    bool hasInFlight = inFlight.has_value()
                        && inFlight->trackId == t.id
                        && inFlight->events
                        && !inFlight->events->empty()
                        && t.sourceType == TrackSourceType::Instrument;

    // "no loop" hint is gated on actual recorded content (committed
    // OR in-flight), not on the presence of a loop region — once
    // there's something to show, show it.
    auto* take = t.loops.empty() ? nullptr : t.loops[0].activeTake();
    bool hasCommitted = take && !take->events.empty();
    if (!hasCommitted && !hasInFlight) {
        paintEmptyRow(g, row.timelineBounds);
        return;
    }
    // Loop region may not exist yet during a bootstrap-mode capture —
    // synthesize an empty stand-in so the rest of the renderer has a
    // shell. lengthBeats falls back to the cycle below.
    static const RegionState kEmptyLoop{};
    const RegionState& loop = t.loops.empty() ? kEmptyLoop : t.loops[0];

    // Render the loop's content repeated across the cycle per the
    // playback rule. If the loop is longer than the cycle, the tail
    // past cycleBeats gets a faded overlay. lengthBeats can still be 0
    // in the brief window between event capture and length assignment;
    // fall back to the cycle so the take stays visible.
    double cyc = cycleBeats();
    double loopLen = loop.lengthBeats > 0.0 ? loop.lengthBeats : cyc;

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

        // Live overlay: in-flight capture events painted on the first
        // rep only (they're cycle-relative — see beginCapture(0.0)).
        // Brighter than committed notes so the live activity reads as
        // "this is happening right now."
        if (rep == 0 && hasInFlight) {
            paintNotes(g, repBounds, *inFlight->events, loopLen,
                       Theme::color(Theme::Color::triggerLight).withAlpha(0.95f));
        }

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
    if (!take) return;
    paintNotes(g, bounds, take->events, loop.lengthBeats,
               Theme::color(Theme::Color::textPrimary).withAlpha(0.85f));
}

void LooperPane::paintNotes(juce::Graphics& g, juce::Rectangle<int> bounds,
                              const std::vector<MidiEventState>& events,
                              double lengthBeats,
                              juce::Colour color) {
    if (events.empty() || lengthBeats <= 0.0) return;

    // Simple piano-roll-ish render: find pitch range, map beats to x,
    // pitches to y. Keeps the pane understandable without importing
    // ProducePane's full-fidelity renderer.
    int minPitch = 127, maxPitch = 0;
    for (auto& e : events) {
        if ((e.status & 0xF0) != 0x90 || e.data2 == 0) continue;
        if (e.data1 < minPitch) minPitch = e.data1;
        if (e.data1 > maxPitch) maxPitch = e.data1;
    }
    if (minPitch > maxPitch) return;
    int pitchRange = std::max(1, maxPitch - minPitch);

    struct Note { double startBeat, endBeat; int pitch; };
    std::vector<Note> notes;
    std::map<int, double> open;
    for (auto& e : events) {
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
    // In-flight notes that haven't seen a noteOff yet — extend to the
    // end of the loop so the user sees the in-progress bar growing
    // toward the playhead.
    for (auto& [pitch, onBeat] : open)
        notes.push_back({ onBeat, lengthBeats, pitch });

    g.setColour(color);
    for (auto& n : notes) {
        double xFrac0 = n.startBeat / lengthBeats;
        double xFrac1 = n.endBeat   / lengthBeats;
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
    g.drawText(juce::String::fromUTF8("no loop \xe2\x80\x94 focus this track, click \xe2\x80\x9creplace\xe2\x80\x9d above to capture"),
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
    int fillX0 = timeline.getX();
    if (x <= fillX0) {
        // Nothing to fill yet — just draw the playhead line and bail.
        g.setColour(Theme::color(Theme::Color::playhead));
        g.fillRect(x, topY, 2, botY - topY);
        return;
    }

    // Per-row progress fill. Most rows get the subtle accent tint, but
    // a row with a queued/capturing loop action gets the action-color
    // tint instead — pulsing while queued, solid while capturing. The
    // tint lives in the same left-of-playhead region as the cycle
    // progress so the two visuals are consistent.
    const auto neutral = Theme::color(Theme::Color::accent).withAlpha(0.10f);
    const auto replaceColor = juce::Colour(0xffcc6655);
    const auto overdubColor = juce::Colour(0xff5fb09f);

    // Pulse phase: 0..1, oscillating ~1Hz, used to modulate alpha.
    double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    float pulse = 0.5f + 0.5f * (float) std::sin(t * juce::MathConstants<double>::twoPi);

    for (auto& row : rowGeoms) {
        auto act = state.getLoopAction(row.trackId);
        juce::Colour fill = neutral;
        // Tap-to-start, tap-to-stop: there's no queued state any more —
        // the only non-None states are Capturing*. Pulse during capture
        // (was the queued look) so the eye still has a "this is live and
        // happening" cue without screaming SOLID at the user.
        switch (act) {
            case LoopAction::CapturingReplace:
                fill = replaceColor.withAlpha(0.10f + 0.18f * pulse);
                break;
            case LoopAction::CapturingOverdub:
                fill = overdubColor.withAlpha(0.10f + 0.18f * pulse);
                break;
            default:
                break;
        }
        g.setColour(fill);
        g.fillRect(fillX0, row.rowBounds.getY(),
                   x - fillX0, row.rowBounds.getHeight());
    }

    // Vertical playhead line, drawn last so it sits on top of the fill.
    g.setColour(Theme::color(Theme::Color::playhead));
    g.fillRect(x, topY, 2, botY - topY);
}

// ---- Interaction ----------------------------------------------------------

void LooperPane::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    auto pos = e.getPosition();

    // (Loop length is captured by tap-to-stop, not chosen — the readout
    // pill is display-only.)
    // Top bar — MIDI Learn toggle.
    if (learnPill.contains(pos)) {
        toggleLearnMode();
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
    // Top bar — gesture buttons. Same calls as the bindable actions.
    // (Gesture button clicks are handled by the BindableButton child
    // components themselves; clicks land here only on empty top-bar
    // background or fall through to row clicks below.)

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
    // Escape exits MIDI Learn mode without binding anything.
    if (key.isKeyCode(juce::KeyPress::escapeKey) && learnMode) {
        exitLearnMode();
        return true;
    }
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

// ---- MIDI Learn -----------------------------------------------------------

void LooperPane::toggleLearnMode() {
    if (learnMode) exitLearnMode();
    else {
        learnMode = true;
        armedActionName.clear();   // nothing armed until the user clicks a cell
        repaint();
    }
}

void LooperPane::exitLearnMode() {
    learnMode = false;
    armedActionName.clear();
    coord.cancelMidiLearn();
    repaint();
}

void LooperPane::armForLearn(const juce::String& actionName) {
    armedActionName = actionName;
    rearmLearnCapture();
    repaint();
}

void LooperPane::rearmLearnCapture() {
    // Empty deviceId = listen across every enabled MIDI input.
    coord.startMidiLearn("",
        [safe = juce::Component::SafePointer<LooperPane>(this)]
        (const std::string& type, int ch, int num, const std::string& port) {
            juce::MessageManager::callAsync([safe, type, ch, num, port]() {
                if (safe) safe->onLearnCapture(type, ch, num, port);
            });
        });
}

void LooperPane::onLearnCapture(const std::string& type, int channel, int number,
                                 const std::string& portName) {
    // Bail if state changed under us mid-capture.
    if (!learnMode || armedActionName.isEmpty()) return;

    // Resolve / register the source device. Mirrors ControllersPane's
    // learn flow so devices end up in the same registry. IAC loopback
    // is noise — re-arm and ignore.
    DeviceId deviceId;
    if (!portName.empty()) {
        if (juce::String(portName).containsIgnoreCase("IAC Driver")) {
            rearmLearnCapture();
            return;
        }
        if (auto* dev = state.findDeviceByPortName(portName)) {
            deviceId = dev->id;
        } else {
            deviceId = state.registerDevice(portName, portName);
        }
    }
    if (deviceId.empty()) {
        rearmLearnCapture();
        return;
    }

    // Make sure the control exists on the device — if not, register a
    // default-named control for it so it shows up in Mappings later.
    bool haveControl = false;
    if (auto* dev = state.findDevice(deviceId)) {
        for (auto& ctrl : dev->controls)
            if (ctrl.controlType == type && ctrl.channel == channel && ctrl.number == number) {
                haveControl = true; break;
            }
    }
    if (!haveControl) {
        juce::String defaultName = (type == "cc")   ? "CC " + juce::String(number)
                                  : (type == "note") ? "Note " + juce::String(number)
                                                     : juce::String("Control");
        state.addDeviceControl(deviceId, defaultName.toStdString(), type, channel, number);
    }

    auto* a = state.findActionByName(armedActionName.toStdString());
    auto* song = state.currentSong();
    if (!a || !song) {
        armedActionName.clear();
        repaint();
        return;
    }

    // 1:1 binding per action (the rule the user picked earlier in the
    // gesture-button design discussion). Drop any existing song-scoped
    // bindings for this action before adding the new one.
    for (auto& b : state.bindingsForSong(song->id))
        if (b.actionId == a->id) state.removeBinding(b.id);

    juce::String desc = (type == "cc")   ? "CC " + juce::String(number)
                       : (type == "note") ? "Note " + juce::String(number)
                                          : juce::String(type);
    state.addBinding(song->id, type, channel, number, a->id, "[]",
                      desc.toStdString(), deviceId);

    // Stay in learn mode but clear the armed cell — the user can click
    // another to keep building a bank without leaving the mode.
    armedActionName.clear();
    coord.cancelMidiLearn();
    repaint();
}
