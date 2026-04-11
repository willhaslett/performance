#include "gui/ProducePane.h"
#include "api/StateAPI.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

ProducePane::ProducePane() {
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

ProducePane::~ProducePane() {
    if (state && stateSubscriptionId >= 0)
        state->events().unsubscribe(stateSubscriptionId);
}

void ProducePane::setState(StateAPI* s, SequencerAPI* seq, Arrangement* arr) {
    state = s;
    sequencer = seq;
    arrangement = arr;

    if (state) {
        stateSubscriptionId = state->events().subscribe([this](const StateEvent& event) {
            if (event.entity == StateEvent::Track || event.entity == StateEvent::Config)
                juce::MessageManager::callAsync([this] { repaint(); });
        });
    }
}

void ProducePane::timerCallback() {
    if (sequencer && sequencer->isPlaying()) {
        // Auto-scroll (Logic-style page scroll):
        // When playhead reaches ~1 bar before the right edge, jump forward
        // by the full visible beat range so bar positions stay stable.
        double beat = sequencer->getBeatPosition();
        int gridWidth = getWidth() - trackHeaderWidth;
        double visibleBeats = gridWidth / pixelsPerBeat;
        double rightEdgeBeat = scrollBeat + visibleBeats;
        double threshold = rightEdgeBeat - beatsPerBar();
        if (beat >= threshold) {
            // Snap scroll position to bar boundary for visual stability
            int bpb = beatsPerBar();
            double newScroll = std::floor(beat / bpb) * bpb;
            scrollBeat = std::max(0.0, newScroll);
        }
        repaint();
    }
}

int ProducePane::beatsPerBar() const {
    return sequencer ? sequencer->getTimeSignatureNumerator() : 4;
}

int ProducePane::beatToX(double beat) const {
    return trackHeaderWidth + (int)((beat - scrollBeat) * pixelsPerBeat);
}

double ProducePane::xToBeat(int x) const {
    return scrollBeat + (double)(x - trackHeaderWidth) / pixelsPerBeat;
}

// --- Paint ---

void ProducePane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    auto area = getLocalBounds();

    // Transport bar at top
    auto transportArea = area.removeFromTop(transportHeight);
    paintTransport(g, transportArea);

    // Ruler below transport
    auto rulerArea = area.removeFromTop(rulerHeight);
    paintRuler(g, rulerArea);

    // Track headers on left, grid on right
    auto trackArea = area.removeFromLeft(trackHeaderWidth);
    paintTrackHeaders(g, trackArea);
    {
        juce::Graphics::ScopedSaveState sss(g);
        paintGrid(g, area);
    }

    // Drag reorder indicator
    if (dragTrackIndex >= 0 && dragTargetIndex >= 0 && dragTrackIndex != dragTargetIndex) {
        int gridTop = transportHeight + rulerHeight;
        int indicatorY = gridTop + dragTargetIndex * trackRowHeight;
        if (dragTargetIndex > dragTrackIndex) indicatorY += trackRowHeight;

        // Full-width indicator line
        g.setColour(Theme::color(Theme::Color::accent));
        g.fillRect(0, indicatorY - 2, getWidth(), 5);

        // Dim the source track row
        int srcY = gridTop + dragTrackIndex * trackRowHeight;
        g.setColour(juce::Colour(0x40000000));
        g.fillRect(0, srcY, getWidth(), trackRowHeight);
    }

    // Playhead overlaid
    if (sequencer) {
        auto fullGridArea = getLocalBounds()
            .withTrimmedTop(transportHeight + rulerHeight)
            .withTrimmedLeft(trackHeaderWidth);
        // Also draw playhead on ruler
        auto fullRulerGrid = getLocalBounds()
            .withTrimmedTop(transportHeight)
            .withTrimmedLeft(trackHeaderWidth);
        paintPlayhead(g, fullRulerGrid);
    }
}

void ProducePane::paintTransport(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(area);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)area.getBottom(), (float)getWidth(), (float)area.getBottom(), 1.0f);

    if (!sequencer) return;

    bool playing = sequencer->isPlaying();
    bool looping = sequencer->isLoopEnabled();
    double bpm = sequencer->getTempo();
    double beat = sequencer->getBeatPosition();

    // Layout: buttons on left, position display centered, tempo on right
    int btnSize = 28;
    int btnY = area.getCentreY() - btnSize / 2;
    int btnX = area.getX() + 10;
    int btnGap = 4;

    // --- Transport buttons ---

    // Rewind (|◀◀)
    rewindButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        auto r = rewindButtonBounds.reduced(6).toFloat();
        // Two small left-pointing triangles + bar
        g.fillRect((int)r.getX(), (int)r.getY(), 2, (int)r.getHeight());
        juce::Path tri;
        tri.addTriangle(r.getX() + 4, r.getCentreY(), r.getRight() - 2, r.getY(),
                         r.getRight() - 2, r.getBottom());
        g.fillPath(tri);
    }
    btnX += btnSize + btnGap;

    // Stop (■)
    stopButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.fillRect(stopButtonBounds.reduced(8));
    }
    btnX += btnSize + btnGap;

    // Play (▶) — green background when playing (Logic style)
    playButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    {
        if (playing) {
            g.setColour(juce::Colour(0xff2a6a2a));  // dark green bg
            g.fillRoundedRectangle(playButtonBounds.toFloat(), 4.0f);
            g.setColour(Theme::color(Theme::Color::textWhite));
        } else {
            g.setColour(Theme::color(Theme::Color::textSecondary));
        }
        juce::Path tri;
        auto r = playButtonBounds.reduced(7).toFloat();
        tri.addTriangle(r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
        g.fillPath(tri);
    }
    btnX += btnSize + btnGap;

    // Record (●) — red/brown background when recording (Logic style)
    bool inRecordMode = onIsRecordMode ? onIsRecordMode() : false;
    recordButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    {
        if (inRecordMode) {
            g.setColour(juce::Colour(0xff6a2a2a));  // dark red bg
            g.fillRoundedRectangle(recordButtonBounds.toFloat(), 4.0f);
            g.setColour(Theme::color(Theme::Color::textWhite));
        } else {
            g.setColour(juce::Colour(0xffcc4444));  // red circle
        }
        auto rb = recordButtonBounds.reduced(7).toFloat();
        g.fillEllipse(rb);
    }
    btnX += btnSize + btnGap + 4;

    // Cycle (⟳)
    cycleButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    {
        auto col = looping ? Theme::color(Theme::Color::accent)
                            : Theme::color(Theme::Color::textDim);
        g.setColour(col);
        auto r = cycleButtonBounds.reduced(5).toFloat();
        juce::Path arc;
        arc.addCentredArc(r.getCentreX(), r.getCentreY(),
                           r.getWidth() * 0.4f, r.getHeight() * 0.4f,
                           0.0f, 0.3f, juce::MathConstants<float>::twoPi - 0.5f, true);
        g.strokePath(arc, juce::PathStrokeType(1.5f));
        // Arrow head at end of arc
        float endAngle = juce::MathConstants<float>::twoPi - 0.5f;
        float ax = r.getCentreX() + r.getWidth() * 0.4f * std::cos(endAngle);
        float ay = r.getCentreY() + r.getHeight() * 0.4f * std::sin(endAngle);
        juce::Path arrow;
        arrow.addTriangle(ax - 3, ay - 4, ax + 3, ay, ax - 1, ay + 4);
        g.fillPath(arrow);
    }

    // --- Position display (LCD-style, centered) ---
    int lcdWidth = 420;
    int lcdX = area.getCentreX() - lcdWidth / 2;
    int lcdY = area.getY() + 8;
    int lcdHeight = area.getHeight() - 16;
    auto lcdBounds = juce::Rectangle<int>(lcdX, lcdY, lcdWidth, lcdHeight);

    // LCD background — matches fader meter groove
    auto lcdBg = Theme::color(Theme::Color::bgSlot);
    auto lcdBorder = Theme::color(Theme::Color::border);
    auto lcdDigit = Theme::color(Theme::Color::midiActive);
    g.setColour(lcdBg);
    g.fillRoundedRectangle(lcdBounds.toFloat(), 4.0f);
    g.setColour(lcdBorder);
    g.drawRoundedRectangle(lcdBounds.toFloat(), 4.0f, 1.0f);

    // Shared layout
    int digitTop = lcdBounds.getY() + 2;
    int digitH = lcdBounds.getHeight() - 14;
    int labelY = lcdBounds.getBottom() - 13;
    auto monoLg = Theme::fontMono(22.0f);
    auto monoMd = Theme::fontMono(18.0f);
    auto labelFont = Theme::font(8.0f);
    char buf[16];

    // --- Beat position: BAR . BEAT . DIV . TICK ---
    int bar = (int)(beat / beatsPerBar()) + 1;
    int beatInBar = (int)std::fmod(beat, (double)beatsPerBar()) + 1;
    double fractional = std::fmod(beat, 1.0);
    int tsDen = sequencer->getTimeSignatureDenominator();
    int div = (int)(fractional * tsDen) + 1;
    int tick = (int)(std::fmod(fractional * tsDen, 1.0) * 240);

    int colX = lcdBounds.getX() + 6;

    auto drawCol = [&](const char* text, const char* label, int width, juce::Font font) {
        g.setFont(font);
        g.setColour(lcdDigit);
        g.drawText(text, colX, digitTop, width, digitH, juce::Justification::centred);
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(labelFont);
        g.drawText(label, colX, labelY, width, 12, juce::Justification::centred);
        colX += width;
    };

    auto drawSep = [&]() {
        g.setColour(lcdBorder);
        g.drawLine((float)colX, (float)(lcdBounds.getY() + 4),
                   (float)colX, (float)(lcdBounds.getBottom() - 4), 1.0f);
        colX += 6;
    };

    snprintf(buf, sizeof(buf), "%3d", bar);
    drawCol(buf, "BAR", 46, monoLg);
    snprintf(buf, sizeof(buf), "%d", beatInBar);
    drawCol(buf, "BEAT", 30, monoLg);
    snprintf(buf, sizeof(buf), "%d", div);
    drawCol(buf, "DIV", 26, monoLg);
    snprintf(buf, sizeof(buf), "%03d", tick);
    drawCol(buf, "TICK", 42, monoLg);

    drawSep();

    // --- Time: HH : MM : SS . ms ---
    double totalSeconds = (bpm > 0) ? (beat / (bpm / 60.0)) : 0.0;
    int hrs = (int)(totalSeconds / 3600.0);
    int mins = (int)(std::fmod(totalSeconds, 3600.0) / 60.0);
    int secs = (int)std::fmod(totalSeconds, 60.0);
    int ms = (int)(std::fmod(totalSeconds, 1.0) * 1000.0);

    snprintf(buf, sizeof(buf), "%d:%02d:%02d.%03d", hrs, mins, secs, ms);
    drawCol(buf, "TIME", 150, monoMd);

    drawSep();

    // --- Tempo + Time Signature ---
    snprintf(buf, sizeof(buf), "%.1f", bpm);
    drawCol(buf, "BPM", 52, monoMd);

    colX += 2;
    snprintf(buf, sizeof(buf), "%d/%d",
             sequencer->getTimeSignatureNumerator(), tsDen);
    drawCol(buf, "TIME SIG", 48, monoMd);
}

void ProducePane::paintRuler(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRect(area.withLeft(trackHeaderWidth));

    g.setFont(Theme::font(9.0f));
    int gridWidth = getWidth() - trackHeaderWidth;
    double startBeat = scrollBeat;
    double endBeat = startBeat + gridWidth / pixelsPerBeat;

    // Draw bar numbers
    int startBar = (int)(startBeat / beatsPerBar());
    int endBar = (int)(endBeat / beatsPerBar()) + 1;

    for (int bar = startBar; bar <= endBar; ++bar) {
        double barBeat = bar * beatsPerBar();
        int x = beatToX(barBeat);
        if (x < trackHeaderWidth || x > getWidth()) continue;

        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.drawText(juce::String(bar + 1), x + 2, area.getY(), 30, rulerHeight,
                   juce::Justification::centredLeft);

        g.setColour(Theme::color(Theme::Color::border));
        g.drawLine((float)x, (float)area.getY(), (float)x, (float)area.getBottom(), 1.0f);
    }
}

void ProducePane::paintPowerIcon(juce::Graphics& g, juce::Rectangle<int> iconArea, bool enabled) {
    auto iconColor = enabled ? Theme::color(Theme::Color::textWhite)
                              : Theme::color(Theme::Color::textDim);

    g.setColour(iconColor);
    juce::Path powerIcon;
    auto a = iconArea.reduced(1).toFloat();
    powerIcon.addCentredArc(a.getCentreX(), a.getCentreY(),
                             a.getWidth() * 0.4f, a.getHeight() * 0.4f,
                             0.0f, juce::MathConstants<float>::pi * 0.3f,
                             juce::MathConstants<float>::pi * 1.7f, true);
    g.strokePath(powerIcon, juce::PathStrokeType(1.5f));
    g.drawLine(a.getCentreX(), a.getY() + 1.0f,
               a.getCentreX(), a.getCentreY(), 1.5f);
}

void ProducePane::paintTrackHeaders(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(area);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getRight(), (float)area.getY(),
               (float)area.getRight(), (float)area.getBottom(), 1.0f);

    if (!state) return;

    auto tracks = state->listTracks();
    powerIconBounds.resize(tracks.size());
    int y = area.getY();
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto& t = tracks[i];
        auto row = juce::Rectangle<int>(area.getX(), y, area.getWidth(), trackRowHeight);
        auto* trackState = state->findTrack(t.id);

        // Track header background
        bool enabled = trackState ? trackState->audioEnabled : true;
        bool isAudioInput = trackState && trackState->sourceType == TrackSourceType::AudioInput;
        constexpr float disabledDarken = 0.35f;

        auto headerCol = isAudioInput ? juce::Colour(0xff3a2e18)
                                       : Theme::color(Theme::Color::bgHeader);
        // User-defined color overrides default
        if (trackState && trackState->color != 0)
            headerCol = juce::Colour(trackState->color);
        if (!enabled)
            headerCol = headerCol.interpolatedWith(juce::Colour(0xff181818), 1.0f - disabledDarken);

        // Full row background
        g.setColour(headerCol);
        g.fillRect(row);

        // Row separator
        g.setColour(Theme::color(Theme::Color::border));
        g.drawLine((float)area.getX(), (float)(y + trackRowHeight),
                   (float)area.getRight(), (float)(y + trackRowHeight), 0.5f);

        // Layout: 8px | power(14) | 6px | arm(10) | 6px | track name
        int cx = area.getX() + 8;
        int cy_row = y + trackRowHeight / 2;

        // Power icon
        auto iconBounds = juce::Rectangle<int>(cx, cy_row - 7, 14, 14);
        powerIconBounds[i] = iconBounds;
        paintPowerIcon(g, iconBounds, enabled);
        cx += 14 + 6;

        // Arm dot — only shown when enabled
        bool isArmed = trackState ? trackState->armed : false;
        if (enabled) {
            auto armRect = juce::Rectangle<float>((float)cx, (float)(cy_row - 5), 10.0f, 10.0f);
            if (isArmed) {
                g.setColour(juce::Colour(0xffee8822));
                g.fillEllipse(armRect);
            } else {
                g.setColour(Theme::color(Theme::Color::textDim));
                g.drawEllipse(armRect.reduced(1.0f), 1.5f);
            }
        }
        cx += 10 + 6;

        // Track name
        g.setColour(enabled ? Theme::color(Theme::Color::textPrimary)
                             : Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(juce::String(t.name), cx, y, area.getRight() - cx - 4,
                   trackRowHeight, juce::Justification::centredLeft);

        y += trackRowHeight;
    }
}

void ProducePane::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    g.reduceClipRegion(area);

    if (!state) return;

    auto tracks = state->listTracks();
    int gridWidth = area.getWidth();
    double startBeat = scrollBeat;
    double endBeat = startBeat + gridWidth / pixelsPerBeat;

    // Alternating track lane backgrounds
    for (size_t i = 0; i < tracks.size(); ++i) {
        int rowY = area.getY() + (int)i * trackRowHeight;
        if (i % 2 == 1) {
            g.setColour(juce::Colour(0x08ffffff));
            g.fillRect(area.getX(), rowY, area.getWidth(), trackRowHeight);
        }
    }

    // Grid lines — bar lines darker, beat lines lighter
    int startBar = (int)(startBeat / beatsPerBar());
    int endBar = (int)(endBeat / beatsPerBar()) + 1;

    for (int bar = startBar; bar <= endBar; ++bar) {
        for (int b = 0; b < beatsPerBar(); ++b) {
            double beat = bar * beatsPerBar() + b;
            int x = beatToX(beat);
            if (x < area.getX() || x > area.getRight()) continue;

            g.setColour(b == 0 ? Theme::color(Theme::Color::border)
                                : juce::Colour(0x18ffffff));
            g.drawLine((float)x, (float)area.getY(), (float)x, (float)area.getBottom(), 0.5f);
        }
    }

    // Track row separators
    int y = area.getY();
    for (size_t i = 0; i < tracks.size(); ++i) {
        y += trackRowHeight;
        g.setColour(juce::Colour(0x18ffffff));
        g.drawLine((float)area.getX(), (float)y, (float)area.getRight(), (float)y, 0.5f);
    }

    // Regions
    // Regions — cache hit rects for mouse interaction
    regionHitRects.clear();
    if (arrangement) {
        for (size_t ti = 0; ti < tracks.size(); ++ti) {
            auto regions = arrangement->regionsForTrack(tracks[ti].id);
            std::sort(regions.begin(), regions.end(),
                      [](auto* a, auto* b) { return a->startBeat < b->startBeat; });
            int rowY = area.getY() + (int)ti * trackRowHeight;

            // Resolve track color — matches header color
            auto* trkState = state ? state->findTrack(tracks[ti].id) : nullptr;
            bool isAudioTrk = trkState && trkState->sourceType == TrackSourceType::AudioInput;
            bool trackEnabled = trkState ? trkState->audioEnabled : true;
            uint32_t trackCol = (trkState && trkState->color != 0) ? trkState->color
                              : isAudioTrk ? 0xff3a2e18
                              : Theme::Color::bgHeader;

            for (auto* r : regions) {
                int rx = beatToX(r->startBeat);
                int rw = std::max(4, (int)(r->lengthBeats * pixelsPerBeat));
                auto regionBounds = juce::Rectangle<int>(rx, rowY + 2, rw, trackRowHeight - 4);

                if (regionBounds.getRight() < area.getX() || regionBounds.getX() > area.getRight())
                    continue;

                regionHitRects.push_back({ r->id, tracks[ti].id, regionBounds });

                // Region block — colored by track
                bool selected = (r->id == selectedRegionId);
                bool beingDragged = (draggingRegion && selected);
                auto fillCol = juce::Colour(trackCol);
                if (!trackEnabled || r->muted)
                    fillCol = fillCol.interpolatedWith(juce::Colour(0xff181818), 0.65f);
                float baseAlpha = beingDragged ? 0.45f : 0.82f;
                g.setColour(fillCol.withAlpha(baseAlpha));
                g.fillRoundedRectangle(regionBounds.toFloat(), 5.0f);

                // Border — darker shade of region color (visible at overlaps)
                g.setColour(selected ? Theme::color(Theme::Color::accent)
                                      : fillCol.darker(0.4f));
                g.drawRoundedRectangle(regionBounds.toFloat(), 5.0f,
                                        selected ? 2.0f : 1.0f);

                // Region name
                g.setColour(Theme::color(Theme::Color::textPrimary));
                g.setFont(Theme::font(9.0f));
                g.drawText(juce::String(r->name), regionBounds.reduced(4, 0),
                           juce::Justification::centredLeft);

                // MIDI note preview — mini piano roll
                if (r->type == "midi") {
                    auto noteList = Arrangement::buildNoteList(*r);
                    if (!noteList.empty()) {
                        // Find pitch range for vertical scaling
                        int minNote = 127, maxNote = 0;
                        for (auto& n : noteList) {
                            minNote = std::min(minNote, n.noteNumber);
                            maxNote = std::max(maxNote, n.noteNumber);
                        }
                        int noteRange = std::max(1, maxNote - minNote);
                        // Pad range for visual breathing room
                        int pad = std::max(2, noteRange / 4);
                        int lo = std::max(0, minNote - pad);
                        int hi = std::min(127, maxNote + pad);
                        int span = std::max(1, hi - lo);

                        auto inner = regionBounds.reduced(1, 3);
                        constexpr float noteH = 2.0f;

                        for (auto& note : noteList) {
                            float nx = (float)rx + (float)(note.beatOffset * pixelsPerBeat);
                            float nw = std::max(1.5f, (float)(note.durationBeats * pixelsPerBeat));
                            float ny = inner.getBottom() - ((note.noteNumber - lo) + 0.5f) * ((float)inner.getHeight() / span);

                            // Velocity → brightness: dim to bright, tinted by track color
                            float velNorm = note.velocity / 127.0f;
                            auto noteCol = juce::Colour(trackCol).brighter(0.6f)
                                .interpolatedWith(juce::Colours::white, velNorm * 0.35f)
                                .withAlpha(0.4f + velNorm * 0.5f);
                            g.setColour(noteCol);
                            g.fillRect(nx, ny - noteH * 0.5f, nw, std::max(1.0f, noteH));
                        }
                    }
                }

                // Audio waveform preview
                if (r->type == "audio") {
                    auto* take = r->activeTake();
                    if (take && !take->peakData.peaks.empty()) {
                        auto inner = regionBounds.reduced(1, 3);
                        float centreY = inner.getCentreY();
                        float halfH = inner.getHeight() * 0.5f;
                        auto waveCol = juce::Colour(trackCol).brighter(0.5f).withAlpha(0.7f);
                        g.setColour(waveCol);

                        // Map peaks to pixels
                        int numPeaks = (int)take->peakData.peaks.size();
                        double beatsPerPeak = (take->peakData.samplesPerPeak / (double)take->sampleRate)
                                              * (take->recordTempo / 60.0);
                        for (int pi = 0; pi < numPeaks; ++pi) {
                            float px = (float)rx + (float)(pi * beatsPerPeak * pixelsPerBeat);
                            float pw = std::max(1.0f, (float)(beatsPerPeak * pixelsPerBeat));
                            if (px + pw < inner.getX() || px > inner.getRight()) continue;

                            auto [mn, mx] = take->peakData.peaks[pi];
                            // Nonlinear scaling: sqrt boosts quiet signals
                            float scaledMx = (mx >= 0) ? std::sqrt(mx) : -std::sqrt(-mx);
                            float scaledMn = (mn >= 0) ? std::sqrt(mn) : -std::sqrt(-mn);
                            float y1 = centreY - scaledMx * halfH;
                            float y2 = centreY - scaledMn * halfH;
                            g.drawLine(px, y1, px, y2, pw > 1.5f ? 1.0f : pw);
                        }
                    }
                }
            }
        }

        // Ghost region during drag (move or duplicate)
        if (draggingRegion) {
            auto* srcRegion = arrangement->findRegion(selectedRegionId);
            if (srcRegion) {
                int gx = beatToX(dragCurrentBeat);
                int gw = std::max(4, (int)(srcRegion->lengthBeats * pixelsPerBeat));
                int drawY = area.getY() + dragCurrentTrackIdx * trackRowHeight;
                auto ghostBounds = juce::Rectangle<int>(gx, drawY + 2, gw, trackRowHeight - 4);
                // Use target track's color for ghost
                uint32_t ghostTrackCol = Theme::Color::bgHeader;
                if (dragCurrentTrackIdx >= 0 && dragCurrentTrackIdx < (int)tracks.size()) {
                    auto* ts = state ? state->findTrack(tracks[dragCurrentTrackIdx].id) : nullptr;
                    if (ts && ts->color != 0)
                        ghostTrackCol = ts->color;
                    else if (ts && ts->sourceType == TrackSourceType::AudioInput)
                        ghostTrackCol = 0xff3a2e18;
                }
                auto ghostCol = juce::Colour(ghostTrackCol);
                g.setColour(ghostCol.withAlpha(0.35f));
                g.fillRoundedRectangle(ghostBounds.toFloat(), 5.0f);
                g.setColour(Theme::color(Theme::Color::accent).withAlpha(0.5f));
                g.drawRoundedRectangle(ghostBounds.toFloat(), 5.0f, 1.5f);

                // "+" when option is held (duplicate mode)
                if (dragIsOption) {
                    g.setColour(juce::Colour(0x55ffffff));
                    g.setFont(Theme::fontMono(22.0f));
                    g.drawText("+", ghostBounds, juce::Justification::centred);
                }
            }
        }
    }
}

void ProducePane::paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area) {
    if (!sequencer) return;

    // Interpolate playhead position for smooth rendering between timer ticks
    double beat = sequencer->getBeatPosition();
    // The position is already updated at 10Hz by the coordinator. At 30fps paint,
    // we get ~3 paints per update. The position appears smooth because the timer
    // and paint rates are close enough. For even smoother motion, we could
    // interpolate based on elapsed time and tempo, but this is good enough.
    int x = beatToX(beat);
    if (x < trackHeaderWidth || x > getWidth()) return;

    g.setColour(juce::Colour(0xccffffff));
    g.drawLine((float)x, (float)area.getY(), (float)x, (float)area.getBottom(), 1.0f);

    // Small triangle at top
    juce::Path tri;
    tri.addTriangle((float)x - 4, (float)area.getY(),
                     (float)x + 4, (float)area.getY(),
                     (float)x, (float)(area.getY() + 6));
    g.fillPath(tri);
}

void ProducePane::resized() {}

int ProducePane::getTrackIndexAtY(int y) const {
    int gridTop = transportHeight + rulerHeight;
    if (y < gridTop) return -1;
    int idx = (y - gridTop) / trackRowHeight;
    if (!state) return -1;
    auto tracks = state->listTracks();
    if (idx >= (int)tracks.size()) return -1;
    return idx;
}

void ProducePane::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();

    // Start drag on track header area
    if (event.getPosition().getX() < trackHeaderWidth) {
        int idx = getTrackIndexAtY(event.getPosition().getY());
        if (idx >= 0) {
            dragTrackIndex = idx;
            dragTargetIndex = idx;
            dragStartY = event.getPosition().getY();
        }
        return;
    }

    // Check for region click (grid area, below ruler)
    if (event.getPosition().getY() > transportHeight + rulerHeight
        && event.getPosition().getX() > trackHeaderWidth) {

        // Hit test against cached region bounds
        for (auto& hit : regionHitRects) {
            if (hit.bounds.contains(event.getPosition())) {
                selectedRegionId = hit.regionId;
                draggingRegion = false;  // will start on mouseDrag
                dragIsOption = event.mods.isAltDown();
                dragStartBeat = xToBeat(event.getPosition().getX());
                dragStartTrackIdx = getTrackIndexAtY(event.getPosition().getY());
                dragCurrentBeat = dragStartBeat;
                dragCurrentTrackIdx = dragStartTrackIdx;
                repaint();
                return;
            }
        }

        // Clicked empty grid — deselect and set playhead
        selectedRegionId.clear();
        if (sequencer) {
            double beat = xToBeat(event.getPosition().getX());
            if (beat >= 0.0) sequencer->setBeatPosition(beat);
        }
        repaint();
        return;
    }

    // Click on ruler to set playhead
    if (sequencer && event.getPosition().getY() > transportHeight
        && event.getPosition().getX() > trackHeaderWidth) {
        double beat = xToBeat(event.getPosition().getX());
        if (beat >= 0.0) {
            sequencer->setBeatPosition(beat);
            repaint();
        }
    }
}

void ProducePane::mouseDrag(const juce::MouseEvent& event) {
    // Track header reorder drag
    if (dragTrackIndex >= 0) {
        int newTarget = getTrackIndexAtY(event.getPosition().getY());
        if (newTarget < 0) return;
        if (newTarget != dragTargetIndex) {
            dragTargetIndex = newTarget;
            repaint();
        }
        return;
    }

    // Region drag (starts after 5px threshold)
    if (!selectedRegionId.empty() && sequencer) {
        if (!draggingRegion && event.getDistanceFromDragStart() > 5) {
            draggingRegion = true;
            // Capture the region's original beat for offset calculation
            auto* region = arrangement ? arrangement->findRegion(selectedRegionId) : nullptr;
            if (region) {
                dragStartBeat = region->startBeat;
                dragCurrentBeat = region->startBeat;
            }
        }
        if (draggingRegion) {
            dragIsOption = event.mods.isAltDown();  // live update during drag
            double beatDelta = xToBeat(event.getPosition().getX()) - xToBeat(event.getMouseDownPosition().getX());
            double newBeat = std::max(0.0, dragStartBeat + beatDelta);
            if (snapToGrid) {
                double divSize = sequencer ? 1.0 / sequencer->getTimeSignatureDenominator() : 0.25;
                newBeat = std::round(newBeat / divSize) * divSize;
            }
            dragCurrentBeat = newBeat;

            int trackIdx = getTrackIndexAtY(event.getPosition().getY());
            if (trackIdx >= 0 && state) {
                // Only allow drop on compatible track type
                auto tracks = state->listTracks();
                if (trackIdx < (int)tracks.size()) {
                    auto* targetTrack = state->findTrack(tracks[trackIdx].id);
                    auto* region = arrangement ? arrangement->findRegion(selectedRegionId) : nullptr;
                    if (targetTrack && region) {
                        bool isMidiRegion = (region->type == "midi");
                        bool isInstrumentTrack = (targetTrack->sourceType == TrackSourceType::Instrument);
                        if (isMidiRegion == isInstrumentTrack)
                            dragCurrentTrackIdx = trackIdx;
                        // else: keep previous valid track index (snap back visually)
                    }
                }
            }

            repaint();
        }
    }
}

void ProducePane::mouseUp(const juce::MouseEvent& event) {
    // Complete drag reorder
    if (dragTrackIndex >= 0 && dragTargetIndex >= 0 && dragTrackIndex != dragTargetIndex && state) {
        auto tracks = state->listTracks();
        if (dragTrackIndex < (int)tracks.size() && dragTargetIndex < (int)tracks.size()) {
            auto* srcTrack = state->findTrack(tracks[dragTrackIndex].id);
            auto* dstTrack = state->findTrack(tracks[dragTargetIndex].id);
            if (srcTrack && dstTrack) {
                state->moveTrack(srcTrack->id, dstTrack->position);
            }
        }
        dragTrackIndex = -1;
        dragTargetIndex = -1;
        repaint();
        return;  // don't process other clicks
    }
    dragTrackIndex = -1;
    dragTargetIndex = -1;

    // Complete region drag (move or duplicate)
    if (draggingRegion && arrangement && state) {
        auto tracks = state->listTracks();
        if (dragCurrentTrackIdx >= 0 && dragCurrentTrackIdx < (int)tracks.size()) {
            auto targetTrackId = tracks[dragCurrentTrackIdx].id;
            if (dragIsOption) {
                // Option+drag = duplicate
                auto* newRegion = arrangement->duplicateRegion(selectedRegionId, targetTrackId, dragCurrentBeat);
                if (newRegion) selectedRegionId = newRegion->id;
            } else {
                // Normal drag = move
                arrangement->moveRegion(selectedRegionId, targetTrackId, dragCurrentBeat);
            }
            if (onRegionsChanged) onRegionsChanged();
        }
        draggingRegion = false;
        repaint();
        return;
    }
    draggingRegion = false;

    // Right-click on region — context menu
    if (event.mods.isPopupMenu() && !selectedRegionId.empty() && arrangement) {
        auto* region = arrangement->findRegion(selectedRegionId);
        if (region) {
            for (auto& hit : regionHitRects) {
                if (hit.regionId == selectedRegionId && hit.bounds.contains(event.getPosition())) {
                    juce::PopupMenu menu;
                    menu.addItem(1, region->muted ? "Unmute Region" : "Mute Region");
                    menu.addItem(2, "Delete Region");
                    auto regionId = selectedRegionId;
                    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                        juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
                        [this, regionId](int result) {
                            if (!arrangement) return;
                            if (result == 1) {
                                auto* r = arrangement->findRegion(regionId);
                                if (r) r->muted = !r->muted;
                            } else if (result == 2) {
                                arrangement->removeRegion(regionId);
                                if (selectedRegionId == regionId) selectedRegionId.clear();
                                if (onRegionsChanged) onRegionsChanged();
                            }
                            repaint();
                        });
                    return;
                }
            }
        }
    }

    // Track header controls (power icon, arm dot)
    if (state && event.getPosition().getX() < trackHeaderWidth) {
        int idx = getTrackIndexAtY(event.getPosition().getY());
        if (idx >= 0) {
            auto tracks = state->listTracks();
            if (idx < (int)tracks.size()) {
                auto* trackState = state->findTrack(tracks[idx].id);

                // Power icon
                if (idx < (int)powerIconBounds.size()
                    && powerIconBounds[idx].expanded(6).contains(event.getPosition())) {
                    if (trackState) {
                        bool newEnabled = !trackState->audioEnabled;
                        state->setTrackAudioEnabled(tracks[idx].id, newEnabled);
                        if (newEnabled && trackState->sourceType == TrackSourceType::Instrument)
                            state->setTrackMidiEnabled(tracks[idx].id, true);
                        if (!newEnabled && trackState->armed)
                            state->setTrackArmed(tracks[idx].id, false);
                    }
                    repaint();
                    return;
                }

                // Arm dot — only when track is enabled
                if (trackState && trackState->audioEnabled) {
                    // Layout: 8 + 14(power) + 6 = 28, arm at 28..38
                    int gridTop = transportHeight + rulerHeight;
                    auto armArea = juce::Rectangle<int>(28, gridTop + idx * trackRowHeight + (trackRowHeight - 10) / 2, 10, 10);
                    if (armArea.expanded(4).contains(event.getPosition())) {
                        state->setTrackArmed(tracks[idx].id, !trackState->armed);
                        repaint();
                        return;
                    }
                }
            }
        }
    }

    if (!sequencer) return;

    // Transport buttons
    if (rewindButtonBounds.contains(event.getPosition())) {
        sequencer->setBeatPosition(0.0);
        repaint();
        return;
    }
    if (stopButtonBounds.contains(event.getPosition())) {
        if (sequencer->isPlaying()) sequencer->stop();
        else sequencer->setBeatPosition(0.0);  // second stop = rewind
        repaint();
        return;
    }
    if (playButtonBounds.contains(event.getPosition())) {
        sequencer->togglePlayStop();
        repaint();
        return;
    }
    if (recordButtonBounds.contains(event.getPosition())) {
        bool inRec = onIsRecordMode ? onIsRecordMode() : false;
        if (inRec) {
            if (onStopRecordMode) onStopRecordMode();
        } else {
            if (onStartRecordMode) onStartRecordMode();
        }
        repaint();
        return;
    }
    if (cycleButtonBounds.contains(event.getPosition())) {
        sequencer->setLoopEnabled(!sequencer->isLoopEnabled());
        repaint();
        return;
    }
}

void ProducePane::mouseDoubleClick(const juce::MouseEvent& event) {
    if (!state) return;
    if (event.getPosition().getX() >= trackHeaderWidth) return;

    int idx = getTrackIndexAtY(event.getPosition().getY());
    if (idx < 0) return;

    // Don't trigger on power icon double-click
    if (idx < (int)powerIconBounds.size() && powerIconBounds[idx].expanded(6).contains(event.getPosition()))
        return;

    auto tracks = state->listTracks();
    if (idx >= (int)tracks.size()) return;

    int gridTop = transportHeight + rulerHeight;
    auto editBounds = juce::Rectangle<int>(28, gridTop + idx * trackRowHeight + 4,
                                            trackHeaderWidth - 32, trackRowHeight - 8);
    auto trackId = tracks[idx].id;
    nameEditor.onCommit = [this, trackId](const juce::String& newName) {
        if (state) {
            state->renameTrack(trackId, newName.toStdString());
            repaint();
        }
    };
    nameEditor.show(*this, editBounds, juce::String(tracks[idx].name));
}

void ProducePane::mouseWheelMove(const juce::MouseEvent& event,
                                   const juce::MouseWheelDetails& wheel) {
    if (event.mods.isCommandDown()) {
        // Zoom (pinch or Cmd+scroll)
        pixelsPerBeat = juce::jlimit(5.0, 100.0, pixelsPerBeat + wheel.deltaY * 10);
    } else {
        // Horizontal scroll (two-finger swipe or deltaX)
        if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
            scrollBeat = std::max(0.0, scrollBeat - wheel.deltaX * 4);
        else
            scrollBeat = std::max(0.0, scrollBeat - wheel.deltaY * 4);
    }
    repaint();
}

bool ProducePane::keyPressed(const juce::KeyPress& key) {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused && dynamic_cast<juce::TextEditor*>(focused) != nullptr)
        return false;

    if (key == juce::KeyPress::spaceKey && sequencer) {
        sequencer->togglePlayStop();
        repaint();
        return true;
    }

    // Delete/Backspace: delete selected region
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && !selectedRegionId.empty() && arrangement) {
        arrangement->removeRegion(selectedRegionId);
        selectedRegionId.clear();
        if (onRegionsChanged) onRegionsChanged();
        repaint();
        return true;
    }

    // m: toggle metronome, Shift+M increases vol, (future: decrease)
    if (key.getTextCharacter() == 'm' && sequencer) {
        sequencer->setMetronomeEnabled(!sequencer->isMetronomeEnabled());
        repaint();
        return true;
    }

    // Cmd+D: duplicate selected region (place after original)
    if (key.getTextCharacter() == 'd' && key.getModifiers().isCommandDown()
        && !selectedRegionId.empty() && arrangement) {
        auto* region = arrangement->findRegion(selectedRegionId);
        if (region) {
            // Find which track owns this region
            std::string ownerTrackId;
            if (state) {
                auto tracks = state->listTracks();
                for (auto& t : tracks) {
                    auto regs = arrangement->regionsForTrack(t.id);
                    for (auto* r : regs) {
                        if (r->id == selectedRegionId) { ownerTrackId = t.id; break; }
                    }
                    if (!ownerTrackId.empty()) break;
                }
            }
            if (!ownerTrackId.empty()) {
                auto* dup = arrangement->duplicateRegion(selectedRegionId, ownerTrackId,
                                                          region->startBeat + region->lengthBeats);
                if (dup) selectedRegionId = dup->id;
            }
        }
        if (onRegionsChanged) onRegionsChanged();
        repaint();
        return true;
    }

    // r: start recording
    if (key.getTextCharacter() == 'r' && onStartRecordMode) {
        onStartRecordMode();
        repaint();
        return true;
    }

    // Return: snap playhead to beginning
    if (key == juce::KeyPress::returnKey && sequencer) {
        sequencer->setBeatPosition(0.0);
        repaint();
        return true;
    }

    // h/l: step playhead by one division (1/denominator of a beat)
    if ((key.getTextCharacter() == 'h' || key.getTextCharacter() == 'l') && sequencer) {
        double divSize = 1.0 / sequencer->getTimeSignatureDenominator();
        double beat = sequencer->getBeatPosition();
        if (key.getTextCharacter() == 'h') {
            double snapped = std::floor(beat / divSize) * divSize - divSize;
            sequencer->setBeatPosition(std::max(0.0, snapped));
        } else {
            double snapped = std::floor(beat / divSize) * divSize + divSize;
            sequencer->setBeatPosition(snapped);
        }
        repaint();
        return true;
    }

    return false;
}
