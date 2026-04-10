#include "gui/ProducePane.h"
#include "api/StateAPI.h"
#include "engine/Log.h"

ProducePane::ProducePane() {
    startTimerHz(20);
}

void ProducePane::setState(StateAPI* s, SequencerAPI* seq, Arrangement* arr) {
    state = s;
    sequencer = seq;
    arrangement = arr;
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
    paintGrid(g, area);

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
    double bpm = sequencer->getTempo();
    double beat = sequencer->getBeatPosition();

    // Play/Stop
    playButtonBounds = juce::Rectangle<int>(area.getX() + 8, area.getY() + 6, 24, 24);
    if (playing) {
        g.setColour(Theme::color(Theme::Color::midiActive));
        g.fillRect(playButtonBounds.reduced(5));
    } else {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        juce::Path tri;
        auto r = playButtonBounds.reduced(4).toFloat();
        tri.addTriangle(r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
        g.fillPath(tri);
    }

    // Position: bar.beat
    int bar = (int)(beat / beatsPerBar) + 1;
    int beatInBar = (int)std::fmod(beat, (double)beatsPerBar) + 1;
    g.setColour(Theme::color(Theme::Color::textWhite));
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain));
    char posBuf[16];
    snprintf(posBuf, sizeof(posBuf), "%d . %d", bar, beatInBar);
    g.drawText(posBuf, 40, area.getY(), 80, area.getHeight(), juce::Justification::centredLeft);

    // Tempo
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeSm));
    char tempoBuf[16];
    snprintf(tempoBuf, sizeof(tempoBuf), "%.1f bpm", bpm);
    g.drawText(tempoBuf, 130, area.getY(), 80, area.getHeight(), juce::Justification::centredLeft);
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

void ProducePane::paintTrackHeaders(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(area);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getRight(), (float)area.getY(),
               (float)area.getRight(), (float)area.getBottom(), 1.0f);

    if (!state) return;

    auto tracks = state->listTracks();
    int y = area.getY();
    for (auto& t : tracks) {
        auto row = juce::Rectangle<int>(area.getX(), y, area.getWidth(), trackRowHeight);

        g.setColour(Theme::color(Theme::Color::border));
        g.drawLine((float)area.getX(), (float)(y + trackRowHeight),
                   (float)area.getRight(), (float)(y + trackRowHeight), 0.5f);

        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(juce::String(t.name), row.reduced(8, 0), juce::Justification::centredLeft);

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

    double beat = sequencer->getBeatPosition();
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

void ProducePane::mouseUp(const juce::MouseEvent& event) {
    if (!sequencer) return;

    // Play button
    if (playButtonBounds.contains(event.getPosition())) {
        sequencer->togglePlayStop();
        return;
    }

    // Click on ruler to set position
    auto rulerArea = getLocalBounds().withTrimmedTop(transportHeight).removeFromTop(rulerHeight);
    if (rulerArea.contains(event.getPosition()) && event.getPosition().getX() > trackHeaderWidth) {
        double beat = xToBeat(event.getPosition().getX());
        if (beat >= 0.0) sequencer->setBeatPosition(beat);
    }
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
