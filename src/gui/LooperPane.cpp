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
    // One segmented strip in the top bar: play | replace overdub |
    // undo redo mute clear | prev next | reset. Left corner on the
    // first cell, Right corner on the last, Mid for everything between.
    auto hasFocus = [this]() {
        return !state.getFocusedTrackId().empty();
    };

    playBtn = std::make_unique<BindableButton>(state, coord, "togglePlay", "",
                                                BindableButton::Variant::IconPlay);
    playBtn->setCornerStyle(BindableButton::Left);
    playBtn->setActivePredicate([this] {
        return sequencer && sequencer->isPlaying();
    });
    addAndMakeVisible(*playBtn);

    replaceBtn = std::make_unique<BindableButton>(state, coord, "replaceLoop", "replace");
    replaceBtn->setCornerStyle(BindableButton::Mid);
    replaceBtn->setEnabledPredicate(hasFocus);
    replaceBtn->setShowRecordDot(true);
    addAndMakeVisible(*replaceBtn);

    overdubBtn = std::make_unique<BindableButton>(state, coord, "overdubLoop", "overdub");
    overdubBtn->setCornerStyle(BindableButton::Mid);
    overdubBtn->setEnabledPredicate(hasFocus);
    overdubBtn->setShowRecordDot(true);
    addAndMakeVisible(*overdubBtn);

    // Loop undo/redo route through app-level UndoHistory so a single
    // snapshot covers events + lengthBeats + cycle. The Looper buttons
    // are convenience surface that fires the same path as Cmd-Z/Cmd-⇧Z.
    undoBtn = std::make_unique<BindableButton>(state, coord, "undoLoop", "undo");
    undoBtn->setCornerStyle(BindableButton::Mid);
    undoBtn->setEnabledPredicate([this] { return state.canUndo(); });
    addAndMakeVisible(*undoBtn);

    redoBtn = std::make_unique<BindableButton>(state, coord, "redoLoop", "redo");
    redoBtn->setCornerStyle(BindableButton::Mid);
    redoBtn->setEnabledPredicate([this] { return state.canRedo(); });
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
    focusNextBtn->setCornerStyle(BindableButton::Mid);
    addAndMakeVisible(*focusNextBtn);

    resetBtn = std::make_unique<BindableButton>(state, coord, "resetLooperSession", "reset");
    resetBtn->setCornerStyle(BindableButton::Right);
    addAndMakeVisible(*resetBtn);

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

    // Top-bar layout: a single segmented BindableButton strip, then
    // the LCD. Strip is left-anchored at headerWidth so the leftmost
    // button (play) aligns with the right edge of the track-header
    // column.
    int topBarMid = topBarHeight / 2;

    int bbH = BindableButton::desiredHeight;
    int bbY = topBarMid - bbH / 2;
    const int playWidth = 56;
    const int wideBtn   = 84;   // replace, overdub
    const int narrowBtn = 60;   // undo, redo, mute, clear, reset
    const int iconBtn   = 40;   // focus prev/next

    int x = headerWidth;
    playBtn->setBounds(x, bbY, playWidth, bbH);    x += playWidth;
    focusPrevBtn->setBounds(x, bbY, iconBtn, bbH);  x += iconBtn;
    focusNextBtn->setBounds(x, bbY, iconBtn, bbH);  x += iconBtn;
    replaceBtn->setBounds(x, bbY, wideBtn, bbH);    x += wideBtn;
    overdubBtn->setBounds(x, bbY, wideBtn, bbH);    x += wideBtn;
    undoBtn->setBounds(x, bbY, narrowBtn, bbH);     x += narrowBtn;
    redoBtn->setBounds(x, bbY, narrowBtn, bbH);     x += narrowBtn;
    muteBtn->setBounds(x, bbY, narrowBtn, bbH);     x += narrowBtn;
    clearBtn->setBounds(x, bbY, narrowBtn, bbH);    x += narrowBtn;
    resetBtn->setBounds(x, bbY, narrowBtn, bbH);
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
    // playhead — see paintPlayhead.)

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

    // Pull the in-flight gesture captures (if either targets this
    // track) so the user sees content appear as they record, not only
    // after the cycle wraps.
    auto inFlight = coord.getInFlightLoopCapture();
    bool hasInFlightMidi = inFlight.has_value()
                            && inFlight->trackId == t.id
                            && inFlight->events
                            && !inFlight->events->empty()
                            && t.sourceType == TrackSourceType::Instrument;
    auto inAudio = coord.getInFlightLoopAudio();
    bool hasInFlightAudio = inAudio.has_value()
                             && inAudio->trackId == t.id
                             && t.sourceType == TrackSourceType::AudioInput;
    bool hasInFlight = hasInFlightMidi || hasInFlightAudio;

    // "no loop" hint is gated on actual recorded content (committed
    // OR in-flight), not on the presence of a loop region. Audio loops
    // carry their content in a file (filePath / peaks), not in events,
    // so check for either. Replace-in-progress visually clears the
    // existing content — it's about to be discarded on commit, and
    // showing it during recording reads as "this is still here" when
    // it isn't.
    auto* take = t.loops.empty() ? nullptr : t.loops[0].activeTake();
    bool isAudioLoop  = ! t.loops.empty() && t.loops[0].type == "audio";
    bool replacing    = ! t.loops.empty()
                        && t.loops[0].loopAction == LoopAction::CapturingReplace;
    bool hasCommitted = ! replacing
                        && take
                        && (! take->events.empty()
                            || (isAudioLoop && ! take->filePath.empty()));
    if (! hasCommitted && ! hasInFlight) {
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

        // Region shell — Looper uses the darker bgSlot ("passive inset
        // surface") rather than the Producer's lighter bgSurfaceRaised.
        // The Producer needs the contrast because its regions are
        // bounded blocks against an empty timeline; the Looper's loop
        // region IS the whole lane, so contrast against the row bg
        // would just add noise. Track type is conveyed by the row's
        // left-edge stripe (paintTrackHeader).
        g.setColour(Theme::color(Theme::Color::bgSlot));
        g.fillRoundedRectangle(repBounds.toFloat(), 3.0f);

        // Replace-in-progress: existing committed content is being
        // overwritten — don't paint it (it's already silenced via
        // scanLoopEvents / GraphWrapper's audio gate). Otherwise paint
        // the take normally.
        if (! replacing) {
            if (loop.type == "audio" && take)
                paintAudioWaveform(g, repBounds, *take, loopLen);
            else
                paintLoopNotes(g, repBounds, loop);
        }

        // Live overlay (first rep only — captures are cycle-relative).
        // Open notes / waveform tail cap at the live playhead so the
        // in-progress content grows under the playhead instead of
        // jumping to the right edge.
        if (rep == 0) {
            double headBeat = sequencer ? sequencer->getBeatPosition() : loopLen;
            headBeat = std::clamp(headBeat, 0.0, loopLen);
            if (hasInFlightMidi) {
                paintNotes(g, repBounds, *inFlight->events, loopLen,
                           headBeat,
                           Theme::color(Theme::Color::triggerLight));
            }
            if (hasInFlightAudio) {
                std::vector<std::pair<float, float>> pairPeaks;
                pairPeaks.reserve(inAudio->peaks.size());
                for (auto& p : inAudio->peaks) pairPeaks.push_back({ p.min, p.max });
                paintRawWaveform(g, repBounds, pairPeaks,
                                  inAudio->samplesPerPeak, inAudio->sampleRate,
                                  inAudio->recordTempo, loopLen,
                                  headBeat,
                                  Theme::color(Theme::Color::triggerLight));
            }
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
    // Committed take: any open note (rare) extends to the bar end.
    // Color matches the Producer's MIDI region preview so the visual
    // language is consistent across panes.
    paintNotes(g, bounds, take->events, loop.lengthBeats,
               loop.lengthBeats,
               Theme::color(Theme::Color::typeInstrument));
}

void LooperPane::paintNotes(juce::Graphics& g, juce::Rectangle<int> bounds,
                              const std::vector<MidiEventState>& events,
                              double lengthBeats,
                              double openNoteEndBeat,
                              juce::Colour velocityHueBase) {
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
    // Pad the pitch range a little for vertical breathing room (matches
    // the Producer treatment).
    int range = std::max(1, maxPitch - minPitch);
    int pad   = std::max(2, range / 4);
    int lo    = std::max(0, minPitch - pad);
    int hi    = std::min(127, maxPitch + pad);
    int span  = std::max(1, hi - lo);

    struct Note { double startBeat, endBeat; int pitch; int velocity; };
    std::vector<Note> notes;
    struct OpenNote { double startBeat; int velocity; };
    std::map<int, OpenNote> open;
    for (auto& e : events) {
        if ((e.status & 0xF0) == 0x90 && e.data2 > 0) {
            open[e.data1] = { e.beatOffset, e.data2 };
        } else if ((e.status & 0xF0) == 0x80
                   || ((e.status & 0xF0) == 0x90 && e.data2 == 0)) {
            auto it = open.find(e.data1);
            if (it != open.end()) {
                notes.push_back({ it->second.startBeat, e.beatOffset,
                                  e.data1, it->second.velocity });
                open.erase(it);
            }
        }
    }
    // Open notes (no noteOff yet): extend only to the caller-supplied
    // cap. For the in-flight overlay this is the live playhead, so the
    // in-progress bar grows under the playhead instead of jumping to
    // the right edge of the lane.
    double cap = std::clamp(openNoteEndBeat, 0.0, lengthBeats);
    for (auto& [pitch, on] : open)
        notes.push_back({ on.startBeat, cap, pitch, on.velocity });

    constexpr float noteH = 2.0f;
    auto inner = bounds.toFloat().reduced(1.0f, 3.0f);

    for (auto& n : notes) {
        double xFrac0 = n.startBeat / lengthBeats;
        double xFrac1 = n.endBeat   / lengthBeats;
        float nx = inner.getX() + (float)(xFrac0 * inner.getWidth());
        float nw = std::max(1.5f,
                              (float)((xFrac1 - xFrac0) * inner.getWidth()));
        float ny = inner.getBottom()
                    - ((n.pitch - lo) + 0.5f) * (inner.getHeight() / span);

        // Velocity → brightness + alpha on the base hue (Producer parity).
        float velNorm = juce::jlimit(0.0f, 1.0f, n.velocity / 127.0f);
        auto col = velocityHueBase.darker(0.55f)
                                   .interpolatedWith(velocityHueBase.brighter(0.45f), velNorm)
                                   .withAlpha(0.75f + velNorm * 0.2f);
        g.setColour(col);
        g.fillRect(nx, ny - noteH * 0.5f, nw, std::max(1.0f, noteH));
    }
}

void LooperPane::paintEmptyRow(juce::Graphics& g, juce::Rectangle<int> bounds) {
    // The verbose bootstrap hint is only useful when the looper has no
    // cycle yet. Once anything has been recorded — i.e. master cycle is
    // set — empty tracks should look like quiet, available slots in the
    // session, not like onboarding pages.
    if (state.getCycleLength() <= 0.0) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(juce::String::fromUTF8(
                       "no loop \xe2\x80\x94 focus this track, click "
                       "\xe2\x80\x9creplace\xe2\x80\x9d above to capture"),
                   bounds, juce::Justification::centred);
    }
    // else: leave the lane empty. Row bg + type stripe still anchor it.
}

void LooperPane::paintAudioWaveform(juce::Graphics& g,
                                      juce::Rectangle<int> bounds,
                                      const TakeState& take,
                                      double lengthBeats) {
    paintRawWaveform(g, bounds, take.peakData.peaks,
                     take.peakData.samplesPerPeak, take.sampleRate,
                     take.recordTempo, lengthBeats,
                     /*cap=*/lengthBeats,
                     Theme::color(Theme::Color::typeAudio));
}

void LooperPane::paintRawWaveform(juce::Graphics& g, juce::Rectangle<int> bounds,
                                    const std::vector<std::pair<float, float>>& peaks,
                                    int samplesPerPeak, int sampleRate,
                                    double recordTempo, double lengthBeats,
                                    double cap, juce::Colour baseHue) {
    if (peaks.empty() || lengthBeats <= 0.0 || bounds.getWidth() <= 0
        || sampleRate <= 0)
        return;

    auto inner = bounds.toFloat().reduced(1.0f, 3.0f);
    float centreY = inner.getCentreY();
    float halfH   = inner.getHeight() * 0.5f;

    auto intense = baseHue.brighter(0.25f).withAlpha(0.95f);
    auto dim     = baseHue.darker(0.15f).withAlpha(0.8f);
    juce::ColourGradient grad(intense,
                               inner.getX(), inner.getY(),
                               intense,
                               inner.getX(), inner.getBottom(), false);
    grad.addColour(0.5, dim);
    g.setGradientFill(grad);

    int numPeaks = (int) peaks.size();
    double beatsPerPeak = (samplesPerPeak / (double) sampleRate)
                          * (recordTempo / 60.0);
    double pxPerBeat = (double) bounds.getWidth() / lengthBeats;
    double cappedCap = std::clamp(cap, 0.0, lengthBeats);
    float  capPx     = (float) bounds.getX() + (float)(cappedCap * pxPerBeat);

    for (int pi = 0; pi < numPeaks; ++pi) {
        float px = (float) bounds.getX() + (float)(pi * beatsPerPeak * pxPerBeat);
        float pw = std::max(1.0f, (float)(beatsPerPeak * pxPerBeat));
        if (px + pw < bounds.getX() || px > bounds.getRight()) continue;
        if (px > capPx) break;  // beyond playhead — clip the in-flight tail

        auto [mn, mx] = peaks[pi];
        // sqrt mapping boosts quiet signals visually (matches Producer).
        float scaledMx = (mx >= 0) ? std::sqrt(mx) : -std::sqrt(-mx);
        float scaledMn = (mn >= 0) ? std::sqrt(mn) : -std::sqrt(-mn);
        float y1 = juce::jlimit(inner.getY(), inner.getBottom(),
                                  centreY - scaledMx * halfH);
        float y2 = juce::jlimit(inner.getY(), inner.getBottom(),
                                  centreY - scaledMn * halfH);
        float h  = std::max(1.0f, y2 - y1);
        g.setGradientFill(grad);
        g.fillRect(px, y1, pw, h);

        // Clip indicator — strict clip means the signal exceeded full
        // scale (1.0). "Just about to clip" (== 1.0) reads as a perfect
        // amplitude and stays the normal hue.
        if (mx > 1.0f || mn < -1.0f) {
            g.setColour(Theme::color(Theme::Color::meterRed));
            constexpr float clipStripH = 2.0f;
            if (mx > 1.0f)
                g.fillRect(px, inner.getY(), pw, clipStripH);
            if (mn < -1.0f)
                g.fillRect(px, inner.getBottom() - clipStripH, pw, clipStripH);
        }
    }
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

    // Per-row left-of-playhead fill: a subtle accent trail by default
    // ("you've played this far in the loop"), swapped to the gesture
    // color during Queued / Capturing.
    const auto neutral      = Theme::color(Theme::Color::accent).withAlpha(0.10f);
    const auto replaceColor = juce::Colour(0xffcc6655);
    const auto overdubColor = juce::Colour(0xff5fb09f);

    // Pulse phase: 0..1, oscillating ~1Hz, used to modulate alpha.
    double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    float pulse = 0.5f + 0.5f * (float) std::sin(t * juce::MathConstants<double>::twoPi);

    for (auto& row : rowGeoms) {
        auto act = state.getLoopAction(row.trackId);
        juce::Colour fill = neutral;
        // Queued: subtle pulse — "armed, waiting for the wrap." Existing
        // content still plays + shows. Capturing: stronger pulse — "the
        // cycle owns this; existing content is silenced + hidden, new
        // content is being recorded right now."
        switch (act) {
            case LoopAction::ReplaceQueued:
                fill = replaceColor.withAlpha(0.20f + 0.20f * pulse);
                break;
            case LoopAction::CapturingReplace:
                fill = replaceColor.withAlpha(0.45f + 0.25f * pulse);
                break;
            case LoopAction::OverdubQueued:
                fill = overdubColor.withAlpha(0.20f + 0.20f * pulse);
                break;
            case LoopAction::CapturingOverdub:
                fill = overdubColor.withAlpha(0.45f + 0.25f * pulse);
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

    // (Play, reset, and gesture buttons are BindableButton child
    // components — they handle their own clicks. Top-bar clicks land
    // here only on empty background or fall through to row clicks below.)

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

