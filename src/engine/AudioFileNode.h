#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <map>

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

        double seconds = beatOffset / (entry.recordTempo / 60.0);
        int64_t filePos = (int64_t)(seconds * entry.fileSampleRate);

        int numSamples = buffer.getNumSamples();
        if (filePos >= entry.totalFrames || filePos < 0) {
            buffer.clear();
            return;
        }

        int framesToRead = (int)std::min((int64_t)numSamples, entry.totalFrames - filePos);
        readBuffer.setSize(entry.fileChannels, numSamples, false, false, true);
        entry.reader->read(&readBuffer, 0, framesToRead, filePos, true, true);

        int outChannels = buffer.getNumChannels();
        for (int ch = 0; ch < outChannels; ++ch) {
            int srcCh = std::min(ch, entry.fileChannels - 1);
            buffer.copyFrom(ch, 0, readBuffer, srcCh, 0, framesToRead);
            if (framesToRead < numSamples)
                buffer.clear(ch, framesToRead, numSamples - framesToRead);
        }

        // 5ms fade in/out at region boundaries to prevent clicks
        int fadeSamples = (int)(outputSampleRate * 0.005);
        // Fade in at region start
        if (filePos < fadeSamples) {
            for (int i = 0; i < std::min(fadeSamples - (int)filePos, framesToRead); ++i) {
                float gain = (float)(filePos + i) / fadeSamples;
                for (int ch = 0; ch < outChannels; ++ch)
                    buffer.setSample(ch, i, buffer.getSample(ch, i) * gain);
            }
        }
        // Fade out at region end
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
