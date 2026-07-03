#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/DeviceRateBridge.h"
#include <cmath>

// Wraps an inner AudioProcessor (the graph) so it always runs at a fixed engine
// sample rate while being driven by the audio device at the device rate.
//
//   device callback (deviceRate) ── RateBridgeProcessor ── inner graph (engineRate)
//
// When the two rates match it's a pass-through (zero cost, the common case).
// When they differ — e.g. SCO Bluetooth forces 16k while the project engine
// runs at 48k — it resamples at the boundary via DeviceRateBridge, so plugins
// and synths keep processing at the engine rate regardless of the output device.
// See docs/AUDIO_DEVICE_BOUNDARY.md (phase 4).
//
// Output is pull-driven (the output bridge asks the graph for exactly the engine
// frames it needs → no generation/consumption desync). Live-input resampling is
// best-effort: recording *through* a rate-mismatched device (SCO) is degenerate,
// and monitoring stays continuous.
class RateBridgeProcessor : public juce::AudioProcessor {
public:
    explicit RateBridgeProcessor(juce::AudioProcessor& innerProcessor)
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo())
              .withOutput("Output", juce::AudioChannelSet::stereo())),
          inner(innerProcessor) {
        graphSource.self = this;
        deviceInputSource.self = this;
    }

    // The fixed engine rate the inner graph runs at. Set before prepareToPlay.
    void setEngineSampleRate(double rate) { engineRate = rate; }
    double getEngineSampleRate() const { return engineRate; }
    bool isPassthrough() const { return passthrough; }

    void prepareToPlay(double sampleRate, int blockSize) override {
        deviceRate = sampleRate;
        passthrough = (engineRate <= 0.0) || std::abs(deviceRate - engineRate) < 0.5;

        const int inCh  = getTotalNumInputChannels();
        const int outCh = getTotalNumOutputChannels();
        channels = std::max({ 2, inCh, outCh });

        if (passthrough) {
            inner.setPlayConfigDetails(inCh, outCh, deviceRate, blockSize);
            inner.prepareToPlay(deviceRate, blockSize);
            return;
        }

        // Engine-side block for one device block, worst case, plus margin.
        engineMaxBlock = (int)std::ceil(blockSize * engineRate / deviceRate) + 16;
        inner.setPlayConfigDetails(inCh, outCh, engineRate, engineMaxBlock);
        inner.prepareToPlay(engineRate, engineMaxBlock);

        outputBridge.prepare(channels, engineRate, deviceRate, blockSize);
        inputBridge.prepare(channels, deviceRate, engineRate, engineMaxBlock);

        engineIO.setSize(channels, engineMaxBlock);
        deviceInStash.setSize(channels, blockSize);
        deviceInAvail = 0;
        deviceInCursor = 0;
    }

    void releaseResources() override { inner.releaseResources(); }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        if (passthrough) { inner.processBlock(buffer, midi); return; }

        const int n = buffer.getNumSamples();

        // Stash this block's device-rate input for the input resampler to pull.
        deviceInAvail = n;
        deviceInCursor = 0;
        for (int ch = 0; ch < channels; ++ch) {
            if (ch < buffer.getNumChannels())
                deviceInStash.copyFrom(ch, 0, buffer, ch, 0, n);
            else
                deviceInStash.clear(ch, 0, n);
        }

        // Scale live-MIDI sample positions device→engine; injected once, on the
        // graph chunk below.
        pendingMidi.clear();
        const double midiScale = engineRate / deviceRate;
        for (const auto meta : midi) {
            const int pos = juce::jmax(0, (int)(meta.samplePosition * midiScale));
            pendingMidi.addEvent(meta.getMessage(), pos);
        }
        midiConsumed = false;

        // Output pull drives everything: filling n device frames pulls
        // ~n·engineRate/deviceRate engine frames from the graph, which pulls
        // device input frames through the input bridge.
        outputBridge.process(buffer, n, graphSource);
    }

    const juce::String getName() const override { return "RateBridge"; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
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

private:
    // Yields the device-rate input frames the wrapper stashed this block
    // (zero-padded past the end).
    struct DeviceInputSource : EngineRateSource {
        RateBridgeProcessor* self = nullptr;
        void fillNextBlock(juce::AudioBuffer<float>& dst, int numFrames) override {
            auto* s = self;
            for (int i = 0; i < numFrames; ++i) {
                const int srcIdx = s->deviceInCursor + i;
                const bool have = srcIdx < s->deviceInAvail;
                for (int ch = 0; ch < dst.getNumChannels(); ++ch)
                    dst.setSample(ch, i, (have && ch < s->channels)
                                             ? s->deviceInStash.getSample(ch, srcIdx) : 0.0f);
            }
            s->deviceInCursor += numFrames;
        }
    };

    // Produces engine-rate graph output: resample device input → engine, run the
    // graph in place, hand back its output.
    struct GraphSource : EngineRateSource {
        RateBridgeProcessor* self = nullptr;
        void fillNextBlock(juce::AudioBuffer<float>& dst, int numFrames) override {
            auto* s = self;
            s->engineIO.setSize(s->channels, numFrames, false, false, true);
            s->inputBridge.process(s->engineIO, numFrames, s->deviceInputSource);

            juce::MidiBuffer chunkMidi;
            if (!s->midiConsumed) { chunkMidi.swapWith(s->pendingMidi); s->midiConsumed = true; }
            s->inner.processBlock(s->engineIO, chunkMidi);

            for (int ch = 0; ch < dst.getNumChannels(); ++ch) {
                const int srcCh = std::min(ch, s->channels - 1);
                dst.copyFrom(ch, 0, s->engineIO, srcCh, 0, numFrames);
            }
        }
    };

    juce::AudioProcessor& inner;
    double engineRate = 48000.0, deviceRate = 48000.0;
    int engineMaxBlock = 0, channels = 2;
    bool passthrough = true;

    DeviceRateBridge outputBridge;  // engine → device
    DeviceRateBridge inputBridge;   // device → engine
    juce::AudioBuffer<float> engineIO;      // graph IO at engine rate
    juce::AudioBuffer<float> deviceInStash; // this block's device input
    int deviceInAvail = 0, deviceInCursor = 0;
    juce::MidiBuffer pendingMidi;
    bool midiConsumed = false;

    GraphSource graphSource;
    DeviceInputSource deviceInputSource;
};
