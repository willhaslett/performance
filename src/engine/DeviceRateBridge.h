#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstring>
#include <vector>

// Sample-rate bridge for the engine<->device boundary.
//
// The audio graph runs at a fixed per-project "engine" rate; the hardware runs
// at whatever the device dictates (Bluetooth, an interface, etc.). This class
// converts a stream at `sourceRate` into blocks at `destRate` using JUCE's
// windowed-sinc interpolator, pulling source audio on demand through an
// `EngineRateSource`. See docs/AUDIO_DEVICE_BOUNDARY.md.
//
// Design notes:
//  - Stateful per-channel interpolators carry sub-sample phase across calls, so
//    the output is continuous no matter how the block sizes line up (this is the
//    asynchronous-SRC case: source and dest block counts don't divide evenly).
//  - A per-channel leftover buffer holds the source samples the interpolator
//    hasn't consumed yet, carried to the next call. No allocation in process().
//  - When the rates match, it's a pass-through (no interpolation, no artifacts).

// Fills source-rate audio on demand. Implemented by the graph adapter in the
// real wiring; by a signal generator in tests. Called on the audio thread —
// must not allocate.
struct EngineRateSource {
    virtual ~EngineRateSource() = default;
    // Fill exactly `numFrames` of source-rate audio into channels [0, dest.numCh)
    // of `dest`, starting at sample 0.
    virtual void fillNextBlock(juce::AudioBuffer<float>& dest, int numFrames) = 0;
};

class DeviceRateBridge {
public:
    // sourceRate = engine rate, destRate = device rate. maxDestFrames bounds the
    // largest block process() will be asked to produce (sizes the buffers).
    void prepare(int numChannels, double sourceRate, double destRate, int maxDestFrames) {
        channels = juce::jmax(1, numChannels);
        ratio = (destRate > 0.0) ? sourceRate / destRate : 1.0;
        passthrough = std::abs(sourceRate - destRate) < 1.0e-6;
        maxDest = juce::jmax(1, maxDestFrames);

        // Worst-case source frames for one full dest block, plus headroom for
        // the fractional remainder + carried leftover.
        capacity = (int)std::ceil(ratio * maxDest) + 16;
        staging.setSize(channels, capacity);
        staging.clear();
        pullBuf.setSize(channels, capacity);
        stagingCount = 0;

        interps.clear();
        interps.reserve((size_t)channels);
        for (int i = 0; i < channels; ++i)
            interps.emplace_back();  // non-copyable, so construct in place
        reset();
    }

    void reset() {
        stagingCount = 0;
        staging.clear();
        for (auto& in : interps) in.reset();
    }

    bool isPassthrough() const { return passthrough; }
    double speedRatio() const { return ratio; }

    // Produce `numDest` frames at the device rate into `dest`, pulling source
    // audio from `source` as needed. RT-safe: no allocation.
    void process(juce::AudioBuffer<float>& dest, int numDest, EngineRateSource& source) {
        if (numDest <= 0) return;

        if (passthrough) {
            source.fillNextBlock(dest, numDest);
            return;
        }

        // numDest must not exceed what we sized for; clamp defensively.
        numDest = juce::jmin(numDest, maxDest);

        const int srcNeeded = juce::jmin(capacity, (int)std::ceil(ratio * numDest) + 2);

        // One pull covers the whole deficit (the source can synthesise any count).
        if (stagingCount < srcNeeded) {
            const int deficit = srcNeeded - stagingCount;
            source.fillNextBlock(pullBuf, deficit);
            for (int ch = 0; ch < channels; ++ch)
                staging.copyFrom(ch, stagingCount, pullBuf, ch, 0, deficit);
            stagingCount += deficit;
        }

        int used = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const int u = interps[(size_t)ch].process(ratio,
                                                       staging.getReadPointer(ch),
                                                       dest.getWritePointer(ch),
                                                       numDest);
            if (ch == 0) used = u;  // identical across channels (same ratio + state)
        }

        // Carry the unconsumed tail to the front for the next call.
        const int remaining = juce::jmax(0, stagingCount - used);
        if (remaining > 0 && used > 0) {
            for (int ch = 0; ch < channels; ++ch)
                std::memmove(staging.getWritePointer(ch),
                             staging.getReadPointer(ch) + used,
                             (size_t)remaining * sizeof(float));
        }
        stagingCount = remaining;
    }

private:
    int channels = 0;
    double ratio = 1.0;          // source frames per dest frame
    bool passthrough = false;
    int maxDest = 0;
    int capacity = 0;
    int stagingCount = 0;
    std::vector<juce::Interpolators::WindowedSinc> interps;
    juce::AudioBuffer<float> staging;   // unconsumed source samples
    juce::AudioBuffer<float> pullBuf;   // scratch for source pulls
};
