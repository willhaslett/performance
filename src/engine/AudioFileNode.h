#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <map>
#include <cmath>

// Per-track AudioProcessor for sequencer audio playback.
// Holds multiple loaded WAV files (one per region). The GraphWrapper
// selects which region to play each processBlock based on beat position.

class AudioFileNode : public juce::AudioProcessor {
public:
    AudioFileNode()
        : AudioProcessor(BusesProperties()
              .withOutput("Output", juce::AudioChannelSet::stereo())) {}

    // Load a WAV file for a specific region. Call from message thread.
    bool loadFile(const juce::String& regionId, const juce::String& path,
                  double recordTempo, int fileSampleRate) {
        juce::WavAudioFormat wav;
        auto file = juce::File(path);
        auto stream = file.createInputStream();
        if (!stream) return false;

        auto* newReader = wav.createReaderFor(stream.release(), true);
        if (!newReader) return false;

        auto entry = std::make_unique<FileEntry>();
        entry->reader.reset(newReader);
        entry->recordTempo = recordTempo;
        entry->fileSampleRate = fileSampleRate;
        entry->totalFrames = newReader->lengthInSamples;
        entry->fileChannels = (int)newReader->numChannels;
        files[regionId] = std::move(entry);
        return true;
    }

    void unloadAll() { files.clear(); }

    bool hasFiles() const { return !files.empty(); }

    // Called by GraphWrapper: set which region to play and at what beat
    void setActiveRegion(const juce::String& regionId, double regionStartBeat, double currentBeat) {
        activeRegionId = regionId;
        this->regionStart.store(regionStartBeat, std::memory_order_release);
        this->currentBeat.store(currentBeat, std::memory_order_release);
        active.store(true, std::memory_order_release);
    }

    void setActive(bool a) { active.store(a, std::memory_order_release); }

    void prepareToPlay(double sampleRate, int blockSize) override {
        outputSampleRate = sampleRate;
        readBuffer.setSize(2, blockSize);
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        if (!active.load(std::memory_order_acquire)) {
            buffer.clear();
            return;
        }

        auto it = files.find(activeRegionId);
        if (it == files.end() || !it->second || !it->second->reader) {
            buffer.clear();
            return;
        }

        auto& entry = *it->second;
        double beat = currentBeat.load(std::memory_order_acquire);
        double regStart = regionStart.load(std::memory_order_acquire);
        double beatOffset = beat - regStart;
        if (beatOffset < 0) {
            buffer.clear();
            return;
        }

        const double seconds = beatOffset / (entry.recordTempo / 60.0);
        const int numSamples = buffer.getNumSamples();
        const int outChannels = buffer.getNumChannels();

        // Ratio of file frames to output frames. When the file's stored rate
        // differs from the current output/engine rate we must resample, or the
        // audio plays at the wrong speed and buzzes (block-boundary glitches).
        // See docs/AUDIO_DEVICE_BOUNDARY.md.
        const double ratio = (outputSampleRate > 0.0)
            ? (double)entry.fileSampleRate / outputSampleRate : 1.0;

        if (std::abs(ratio - 1.0) < 1.0e-9) {
            // Fast path: file rate == output rate — bit-exact 1:1 read.
            int64_t filePos = (int64_t)(seconds * entry.fileSampleRate);
            if (filePos >= entry.totalFrames || filePos < 0) {
                buffer.clear();
                return;
            }

            int framesToRead = (int)std::min((int64_t)numSamples, entry.totalFrames - filePos);
            readBuffer.setSize(entry.fileChannels, numSamples, false, false, true);
            entry.reader->read(&readBuffer, 0, framesToRead, filePos, true, true);

            for (int ch = 0; ch < outChannels; ++ch) {
                int srcCh = std::min(ch, entry.fileChannels - 1);
                buffer.copyFrom(ch, 0, readBuffer, srcCh, 0, framesToRead);
                if (framesToRead < numSamples)
                    buffer.clear(ch, framesToRead, numSamples - framesToRead);
            }

            // 5ms fade in/out at region boundaries to prevent clicks
            int fadeSamples = (int)(outputSampleRate * 0.005);
            if (filePos < fadeSamples) {
                for (int i = 0; i < std::min(fadeSamples - (int)filePos, framesToRead); ++i) {
                    float gain = (float)(filePos + i) / fadeSamples;
                    for (int ch = 0; ch < outChannels; ++ch)
                        buffer.setSample(ch, i, buffer.getSample(ch, i) * gain);
                }
            }
            int64_t fadeOutStart = entry.totalFrames - fadeSamples;
            if (filePos + framesToRead > fadeOutStart) {
                int startSample = std::max(0, (int)(fadeOutStart - filePos));
                for (int i = startSample; i < framesToRead; ++i) {
                    float remaining = (float)(entry.totalFrames - filePos - i) / fadeSamples;
                    float gain = std::max(0.0f, std::min(1.0f, remaining));
                    for (int ch = 0; ch < outChannels; ++ch)
                        buffer.setSample(ch, i, buffer.getSample(ch, i) * gain);
                }
            }
            return;
        }

        // Resample path: stateless 4-point Catmull-Rom cubic from the
        // beat-derived fractional file position. Continuous across blocks
        // because the position comes from the continuous beat; seeks and region
        // changes just jump (no interpolator state to go stale).
        const double srcPos0 = seconds * entry.fileSampleRate;  // fractional file frame at out[0]
        if (srcPos0 >= (double)entry.totalFrames || srcPos0 < 0.0) {
            buffer.clear();
            return;
        }

        // Source window covering this block's fractional range, plus one tap on
        // each side for the cubic kernel.
        const int64_t windowStart = std::max((int64_t)0, (int64_t)std::floor(srcPos0) - 1);
        const double lastSrc = srcPos0 + (numSamples - 1) * ratio;
        const int64_t windowEndExcl = std::min(entry.totalFrames, (int64_t)std::floor(lastSrc) + 3);
        const int windowLen = (int)(windowEndExcl - windowStart);
        if (windowLen <= 0) { buffer.clear(); return; }

        readBuffer.setSize(entry.fileChannels, windowLen, false, false, true);
        entry.reader->read(&readBuffer, 0, windowLen, windowStart, true, true);

        const double fadeFrames = entry.fileSampleRate * 0.005;  // 5ms, in file frames
        const float* srcPtr[2] = { nullptr, nullptr };
        for (int ch = 0; ch < entry.fileChannels && ch < 2; ++ch)
            srcPtr[ch] = readBuffer.getReadPointer(ch);

        for (int i = 0; i < numSamples; ++i) {
            const double s = srcPos0 + i * ratio;
            const int64_t idx = (int64_t)std::floor(s);
            const float frac = (float)(s - (double)idx);

            // Boundary fade in file-frame space (never exceeds 1).
            float g = 1.0f;
            if (s < fadeFrames) g = (float)(s / fadeFrames);
            const double fromEnd = (double)entry.totalFrames - s;
            if (fromEnd < fadeFrames) g = std::min(g, (float)(fromEnd / fadeFrames));
            g = juce::jlimit(0.0f, 1.0f, g);

            for (int ch = 0; ch < outChannels; ++ch) {
                const int srcCh = std::min(ch, entry.fileChannels - 1);
                const float* src = srcPtr[srcCh] ? srcPtr[srcCh] : srcPtr[0];
                auto tap = [&](int64_t fi) -> float {
                    const int64_t rel = fi - windowStart;
                    return (src && rel >= 0 && rel < windowLen) ? src[(int)rel] : 0.0f;
                };
                const float y = catmullRom(tap(idx - 1), tap(idx), tap(idx + 1), tap(idx + 2), frac);
                buffer.setSample(ch, i, y * g);
            }
        }
    }

    const juce::String getName() const override { return "AudioFileNode"; }
    double getTailLengthSeconds() const override { return 0; }
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

private:
    // 4-point Catmull-Rom cubic. Interpolates between y1 and y2; t in [0,1).
    static float catmullRom(float y0, float y1, float y2, float y3, float t) {
        const float a0 = y1;
        const float a1 = 0.5f * (y2 - y0);
        const float a2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float a3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((a3 * t + a2) * t + a1) * t + a0;
    }

    struct FileEntry {
        std::unique_ptr<juce::AudioFormatReader> reader;
        double recordTempo = 120.0;
        int fileSampleRate = 48000;
        int fileChannels = 2;
        int64_t totalFrames = 0;
    };
    std::map<juce::String, std::unique_ptr<FileEntry>> files;
    juce::String activeRegionId;
    juce::AudioBuffer<float> readBuffer;
    double outputSampleRate = 48000;

    std::atomic<double> currentBeat { 0.0 };
    std::atomic<double> regionStart { 0.0 };
    std::atomic<bool> active { false };
};
