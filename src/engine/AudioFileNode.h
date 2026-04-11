#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

// Per-track AudioProcessor for sequencer audio playback.
// Reads from a WAV file and outputs audio samples based on beat position.
// The GraphWrapper sets the playback position each processBlock.
// Analogous to MidiSourceNode but for audio data.

class AudioFileNode : public juce::AudioProcessor {
public:
    AudioFileNode()
        : AudioProcessor(BusesProperties()
              .withOutput("Output", juce::AudioChannelSet::stereo())) {}

    // Load a WAV file for playback. Call from message thread.
    bool loadFile(const juce::String& path, double recordTempo, int fileSampleRate) {
        juce::WavAudioFormat wav;
        auto file = juce::File(path);
        auto stream = file.createInputStream();
        if (!stream) return false;

        auto* newReader = wav.createReaderFor(stream.release(), true);
        if (!newReader) return false;

        reader.reset(newReader);
        this->recordTempo = recordTempo;
        this->fileSampleRate = fileSampleRate;
        totalFrames = reader->lengthInSamples;
        fileChannels = (int)reader->numChannels;
        filePath = path;
        return true;
    }

    void unloadFile() {
        reader.reset();
        totalFrames = 0;
        filePath = {};
    }

    bool hasFile() const { return reader != nullptr; }

    // Called by GraphWrapper each processBlock — set the beat range for this buffer
    void setPlaybackBeat(double beat) {
        currentBeat.store(beat, std::memory_order_release);
    }

    // Set the region's start beat so we can compute offset into the file
    void setRegionStartBeat(double beat) {
        regionStart.store(beat, std::memory_order_release);
    }

    // Enable/disable output (when no region covers the current beat)
    void setActive(bool a) {
        active.store(a, std::memory_order_release);
    }

    void prepareToPlay(double sampleRate, int blockSize) override {
        outputSampleRate = sampleRate;
        readBuffer.setSize(2, blockSize);
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        if (!active.load(std::memory_order_acquire) || !reader) {
            buffer.clear();
            return;
        }

        double beat = currentBeat.load(std::memory_order_acquire);
        double regStart = regionStart.load(std::memory_order_acquire);
        double beatOffset = beat - regStart;
        if (beatOffset < 0) {
            buffer.clear();
            return;
        }

        // Convert beat offset to file sample position
        // beats = seconds * (bpm / 60), so seconds = beats / (bpm / 60)
        double seconds = beatOffset / (recordTempo / 60.0);
        int64_t filePos = (int64_t)(seconds * fileSampleRate);

        int numSamples = buffer.getNumSamples();
        if (filePos >= totalFrames || filePos < 0) {
            buffer.clear();
            return;
        }

        // Read from file
        int framesToRead = (int)std::min((int64_t)numSamples, totalFrames - filePos);
        readBuffer.setSize(fileChannels, numSamples, false, false, true);
        reader->read(&readBuffer, 0, framesToRead, filePos, true, true);

        // Copy to output (handle mono→stereo)
        int outChannels = buffer.getNumChannels();
        for (int ch = 0; ch < outChannels; ++ch) {
            int srcCh = std::min(ch, fileChannels - 1);
            buffer.copyFrom(ch, 0, readBuffer, srcCh, 0, framesToRead);
            if (framesToRead < numSamples)
                buffer.clear(ch, framesToRead, numSamples - framesToRead);
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
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::AudioBuffer<float> readBuffer;
    juce::String filePath;

    double recordTempo = 120.0;
    int fileSampleRate = 48000;
    int fileChannels = 2;
    int64_t totalFrames = 0;
    double outputSampleRate = 48000;

    std::atomic<double> currentBeat { 0.0 };
    std::atomic<double> regionStart { 0.0 };
    std::atomic<bool> active { false };
};
