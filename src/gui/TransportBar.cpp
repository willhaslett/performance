#include "gui/TransportBar.h"

TransportBar::TransportBar() {
    startTimerHz(15);  // update display at 15fps
}

void TransportBar::setSequencer(SequencerAPI* seq) {
    sequencer = seq;
    repaint();
}

void TransportBar::timerCallback() {
    if (sequencer) repaint();
}

void TransportBar::paint(juce::Graphics& g) {
    if (!sequencer) return;

    auto area = getLocalBounds();
    bool playing = sequencer->isPlaying();
    double bpm = sequencer->getTempo();
    double beat = sequencer->getBeatPosition();
    int timeSigNum = sequencer->getTimeSignatureNumerator();

    // Play/Stop button
    playBounds = area.removeFromLeft(28).reduced(4);
    if (playing) {
        // Stop icon (square)
        g.setColour(Theme::color(Theme::Color::midiActive));
        g.fillRect(playBounds.reduced(3));
    } else {
        // Play icon (triangle)
        g.setColour(Theme::color(Theme::Color::textSecondary));
        juce::Path tri;
        auto r = playBounds.reduced(2).toFloat();
        tri.addTriangle(r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
        g.fillPath(tri);
    }

    area.removeFromLeft(4);

    // Beat position: bar.beat format
    positionBounds = area.removeFromLeft(70);
    int bar = (int)(beat / timeSigNum) + 1;
    int beatInBar = (int)std::fmod(beat, (double)timeSigNum) + 1;
    g.setColour(Theme::color(Theme::Color::textWhite));
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    char posBuf[16];
    snprintf(posBuf, sizeof(posBuf), "%d . %d", bar, beatInBar);
    g.drawText(posBuf, positionBounds, juce::Justification::centred);

    area.removeFromLeft(4);

    // Tempo
    tempoBounds = area.removeFromLeft(60);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeSm));
    char tempoBuf[16];
    snprintf(tempoBuf, sizeof(tempoBuf), "%.1f", bpm);
    g.drawText(juce::String(tempoBuf) + " bpm", tempoBounds, juce::Justification::centred);

    area.removeFromLeft(4);

    // Loop toggle
    loopBounds = area.removeFromLeft(20);
    bool looping = sequencer->isLoopEnabled();
    g.setColour(looping ? Theme::color(Theme::Color::accent) : Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(Theme::fontSizeXs));
    g.drawText("L", loopBounds, juce::Justification::centred);
}

void TransportBar::resized() {}

void TransportBar::mouseUp(const juce::MouseEvent& event) {
    if (!sequencer) return;

    if (playBounds.contains(event.getPosition())) {
        sequencer->togglePlayStop();
        repaint();
        return;
    }

    if (loopBounds.contains(event.getPosition())) {
        sequencer->setLoopEnabled(!sequencer->isLoopEnabled());
        repaint();
        return;
    }

    // Click tempo to edit (future: inline editor)
    // Click position to reset to 0
    if (positionBounds.contains(event.getPosition())) {
        sequencer->setBeatPosition(0.0);
        repaint();
    }
}
