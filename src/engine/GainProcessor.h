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
        float mag = buffer.getMagnitude(0, buffer.getNumSamples());
        if (mag < 1e-5f) mag = 0.0f;  // noise floor gate
        // Peak hold with exponential decay (~20dB/sec at 44.1kHz/512 block)
        float prev = peakLevel.load(std::memory_order_relaxed);
        float decayed = prev * decayCoeff;
        peakLevel.store(std::max(mag, decayed), std::memory_order_relaxed);
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
    static constexpr float decayCoeff = 0.93f;  // ~20dB/sec decay at typical block rates
};
