#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Per-track MIDI source for sample-accurate sequencer playback.
// The GraphWrapper fills `scheduled` before the graph processes each buffer,
// then processBlock moves it to the output. Both happen on the audio thread
// so no locking is needed.
class MidiSourceNode : public juce::AudioProcessor {
public:
    MidiSourceNode()
        : AudioProcessor(BusesProperties()
              .withOutput("Output", juce::AudioChannelSet::stereo())) {}

    // Called by GraphWrapper before graph->processBlock() (same audio thread)
    void scheduleBuffer(const juce::MidiBuffer& midi) {
        scheduled.addEvents(midi, 0, midi.getLastEventTime() + 1, 0);
    }

    void scheduleSingleMessage(const juce::MidiMessage& msg, int sampleOffset) {
        scheduled.addEvent(msg, sampleOffset);
    }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midiOut) override {
        midiOut.swapWith(scheduled);
        scheduled.clear();
    }

    const juce::String getName() const override { return "MidiSourceNode"; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    juce::MidiBuffer scheduled;
};
