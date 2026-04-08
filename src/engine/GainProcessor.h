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
    float getPeakLevel() const { return std::max(peakL.load(std::memory_order_relaxed),
                                                    peakR.load(std::memory_order_relaxed)); }
    float getPeakL() const { return peakL.load(std::memory_order_relaxed); }
    float getPeakR() const { return peakR.load(std::memory_order_relaxed); }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        buffer.applyGain(gain.load(std::memory_order_relaxed));
        int n = buffer.getNumSamples();
        for (int ch = 0; ch < std::min(buffer.getNumChannels(), 2); ++ch) {
            float mag = buffer.getMagnitude(ch, 0, n);
            if (mag < 1e-5f) mag = 0.0f;
            auto& peak = (ch == 0) ? peakL : peakR;
            float decayed = peak.load(std::memory_order_relaxed) * decayCoeff;
            peak.store(std::max(mag, decayed), std::memory_order_relaxed);
        }
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
    std::atomic<float> peakL { 0.0f };
    std::atomic<float> peakR { 0.0f };
    static constexpr float decayCoeff = 0.93f;
};
