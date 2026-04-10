#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "daw/SequencerAPI.h"

// Compact transport bar: play/stop, tempo, beat position, loop toggle.
// Designed to sit in the toolbar area.

class TransportBar : public juce::Component, private juce::Timer {
public:
    TransportBar();

    void setSequencer(SequencerAPI* seq);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    SequencerAPI* sequencer = nullptr;
    void timerCallback() override;

    juce::Rectangle<int> playBounds, tempoBounds, positionBounds, loopBounds;
};
