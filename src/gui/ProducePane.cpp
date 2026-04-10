#include "gui/ProducePane.h"
#include "api/StateAPI.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

ProducePane::ProducePane() {
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
    if (sequencer && sequencer->isPlaying()) repaint();
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
        auto col = !playing ? Theme::color(Theme::Color::textWhite)
                             : Theme::color(Theme::Color::textSecondary);
        g.setColour(col);
        g.fillRect(stopButtonBounds.reduced(8));
    }
    btnX += btnSize + btnGap;

    // Play (▶)
    playButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    {
        auto col = playing ? Theme::color(Theme::Color::midiActive)
                            : Theme::color(Theme::Color::textSecondary);
        g.setColour(col);
        juce::Path tri;
        auto r = playButtonBounds.reduced(7).toFloat();
        tri.addTriangle(r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
        g.fillPath(tri);
    }
    btnX += btnSize + btnGap + 6;

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
    int lcdY = area.getY() + 4;
    int lcdHeight = area.getHeight() - 8;
    auto lcdBounds = juce::Rectangle<int>(lcdX, lcdY, lcdWidth, lcdHeight);

    // LCD background
    auto lcdBg = juce::Colour(0xff1a1a2a);
    auto lcdBorder = juce::Colour(0xff2a2a3a);
    auto lcdDigit = juce::Colour(0xffddeeff);
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
    int bar = (int)(beat / beatsPerBar) + 1;
    int beatInBar = (int)std::fmod(beat, (double)beatsPerBar) + 1;
    double fractional = std::fmod(beat, 1.0);
    int div = (int)(fractional * 4) + 1;
    int tick = (int)(std::fmod(fractional * 4, 1.0) * 240);

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
    int tsNum = sequencer->getTimeSignatureNumerator();
    int tsDen = sequencer->getTimeSignatureDenominator();
    snprintf(buf, sizeof(buf), "%d/%d", tsNum, tsDen);
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
    int startBar = (int)(startBeat / beatsPerBar);
    int endBar = (int)(endBeat / beatsPerBar) + 1;

    for (int bar = startBar; bar <= endBar; ++bar) {
        double barBeat = bar * beatsPerBar;
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
    auto iconColor = enabled ? Theme::color(Theme::Color::midiActive)
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

        // Track header background — matches mixer header colors
        bool enabled = trackState ? trackState->audioEnabled : true;
        bool isAudioInput = trackState && trackState->sourceType == TrackSourceType::AudioInput;
        constexpr float disabledDarken = 0.35f;

        auto headerCol = isAudioInput ? juce::Colour(0xff8a6a2a)  // amber for audio input
                                       : Theme::color(Theme::Color::bgHeader);
        if (!enabled)
            headerCol = headerCol.interpolatedWith(juce::Colours::black, 1.0f - disabledDarken);

        // Full row background
        g.setColour(headerCol);
        g.fillRect(row);

        // Row separator
        g.setColour(Theme::color(Theme::Color::border));
        g.drawLine((float)area.getX(), (float)(y + trackRowHeight),
                   (float)area.getRight(), (float)(y + trackRowHeight), 0.5f);

        // Power icon
        auto iconBounds = juce::Rectangle<int>(area.getX() + 8, y + (trackRowHeight - 14) / 2, 14, 14);
        powerIconBounds[i] = iconBounds;
        paintPowerIcon(g, iconBounds, enabled);

        // Track name
        g.setColour(enabled ? Theme::color(Theme::Color::textPrimary)
                             : Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(juce::String(t.name), area.getX() + 28, y, area.getWidth() - 32,
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
    int startBar = (int)(startBeat / beatsPerBar);
    int endBar = (int)(endBeat / beatsPerBar) + 1;

    for (int bar = startBar; bar <= endBar; ++bar) {
        for (int b = 0; b < beatsPerBar; ++b) {
            double beat = bar * beatsPerBar + b;
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
    if (arrangement) {
        for (size_t ti = 0; ti < tracks.size(); ++ti) {
            auto regions = arrangement->regionsForTrack(tracks[ti].id);
            int rowY = area.getY() + (int)ti * trackRowHeight;

            for (auto* r : regions) {
                int rx = beatToX(r->startBeat);
                int rw = (int)(r->lengthBeats * pixelsPerBeat);
                auto regionBounds = juce::Rectangle<int>(rx, rowY + 2, rw, trackRowHeight - 4);

                if (regionBounds.getRight() < area.getX() || regionBounds.getX() > area.getRight())
                    continue;

                // Region block
                g.setColour(r->type() == Region::Type::Midi
                    ? juce::Colour(0xff2a5a3a) : juce::Colour(0xff3a3a5a));
                g.fillRoundedRectangle(regionBounds.toFloat(), 3.0f);

                // Region name
                g.setColour(Theme::color(Theme::Color::textPrimary));
                g.setFont(Theme::font(9.0f));
                g.drawText(juce::String(r->name), regionBounds.reduced(4, 0),
                           juce::Justification::centredLeft);

                // MIDI note preview (small vertical lines)
                if (r->type() == Region::Type::Midi) {
                    auto* midi = static_cast<MidiRegion*>(r);
                    g.setColour(juce::Colour(0xff44cc44).withAlpha(0.6f));
                    for (auto& note : midi->notes) {
                        int nx = rx + (int)(note.beatOffset * pixelsPerBeat);
                        int noteY = regionBounds.getBottom() - (int)((note.noteNumber - 36) * 0.3f);
                        noteY = juce::jlimit(regionBounds.getY() + 2, regionBounds.getBottom() - 2, noteY);
                        g.drawLine((float)nx, (float)noteY, (float)nx, (float)(regionBounds.getBottom() - 1), 1.0f);
                    }
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
    // Start drag on track header area
    if (event.getPosition().getX() < trackHeaderWidth) {
        int idx = getTrackIndexAtY(event.getPosition().getY());
        if (idx >= 0) {
            dragTrackIndex = idx;
            dragTargetIndex = idx;
            dragStartY = event.getPosition().getY();
        }
    }
}

void ProducePane::mouseDrag(const juce::MouseEvent& event) {
    if (dragTrackIndex < 0) return;

    int newTarget = getTrackIndexAtY(event.getPosition().getY());
    if (newTarget < 0) return;
    if (newTarget != dragTargetIndex) {
        dragTargetIndex = newTarget;
        repaint();
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

    // Power icon toggle
    if (state && event.getPosition().getX() < trackHeaderWidth) {
        int idx = getTrackIndexAtY(event.getPosition().getY());
        if (idx >= 0 && idx < (int)powerIconBounds.size()) {
            if (powerIconBounds[idx].expanded(6).contains(event.getPosition())) {
                auto tracks = state->listTracks();
                if (idx < (int)tracks.size()) {
                    auto* trackState = state->findTrack(tracks[idx].id);
                    if (trackState) {
                        bool newEnabled = !trackState->audioEnabled;
                        state->setTrackAudioEnabled(tracks[idx].id, newEnabled);
                        if (newEnabled && trackState->sourceType == TrackSourceType::Instrument)
                            state->setTrackMidiEnabled(tracks[idx].id, true);
                    }
                }
                repaint();
                return;
            }
        }
    }

    if (!sequencer) return;

    // Transport buttons
    if (rewindButtonBounds.contains(event.getPosition())) {
        sequencer->setBeatPosition(0.0);
        return;
    }
    if (stopButtonBounds.contains(event.getPosition())) {
        if (sequencer->isPlaying()) sequencer->stop();
        else sequencer->setBeatPosition(0.0);  // second stop = rewind
        return;
    }
    if (playButtonBounds.contains(event.getPosition())) {
        sequencer->togglePlayStop();
        return;
    }
    if (cycleButtonBounds.contains(event.getPosition())) {
        sequencer->setLoopEnabled(!sequencer->isLoopEnabled());
        repaint();
        return;
    }

    // Click on ruler or grid to set playhead position
    if (event.getPosition().getY() > transportHeight && event.getPosition().getX() > trackHeaderWidth) {
        double beat = xToBeat(event.getPosition().getX());
        if (beat >= 0.0) sequencer->setBeatPosition(beat);
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
        // Zoom
        pixelsPerBeat = juce::jlimit(5.0, 100.0, pixelsPerBeat + wheel.deltaY * 10);
    } else {
        // Scroll
        scrollBeat = std::max(0.0, scrollBeat - wheel.deltaY * 4);
    }
    repaint();
}
