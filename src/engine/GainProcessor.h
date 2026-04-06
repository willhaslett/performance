#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

class GainProcessor : public juce::AudioProcessor {
public:
    GainProcessor()
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo())
              .withOutput("Output", juce::AudioChannelSet::stereo())) {}

    void setGain(float g) { gain.store(g, std::memory_order_relaxed); }
    float getGain() const { return gain.load(std::memory_order_relaxed); }
    float getPeakLevel() const { return peakLevel.load(std::memory_order_relaxed); }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        buffer.applyGain(gain.load(std::memory_order_relaxed));
        peakLevel.store(buffer.getMagnitude(0, buffer.getNumSamples()), std::memory_order_relaxed);
    }

    const juce::String getName() const override { return "Gain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    double getTailLengthSeconds() const override { return 0.0; }

private:
    std::atomic<float> gain { 1.0f };
    std::atomic<float> peakLevel { 0.0f };
};
