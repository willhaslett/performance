#pragma once
#include "engine/AudioRecordFIFO.h"
#include "engine/Log.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

// Background thread that drains AudioRecordFIFO to a WAV file.
// Start when recording begins, stop when recording ends.

class AudioWriterThread : private juce::Thread {
public:
    AudioWriterThread() : juce::Thread("AudioWriter") {}

    ~AudioWriterThread() override {
        stopWriting();
    }

    // Begin writing to a new WAV file. Returns true if file was opened.
    bool startWriting(AudioRecordFIFO& fifo, const juce::File& file,
                      double sampleRate, int numChannels) {
        stopWriting();

        recordFIFO = &fifo;
        recordFIFO->reset();
        this->sampleRate = sampleRate;
        this->numChannels = numChannels;
        totalSamplesWritten = 0;
        {
            std::lock_guard<std::mutex> lock(peakMutex);
            peaks.clear();
        }
        peakMin = 0; peakMax = 0; peakSampleCount = 0;

        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        if (!stream) {
            perfLog("[AudioWriter] Failed to create output stream: %s\n",
                    file.getFullPathName().toRawUTF8());
            return false;
        }

        writer.reset(wav.createWriterFor(stream.release(), sampleRate, numChannels,
                                          24, {}, 0));
        if (!writer) {
            perfLog("[AudioWriter] Failed to create WAV writer\n");
            return false;
        }

        shouldStop.store(false);
        startThread(juce::Thread::Priority::normal);
        perfLog("[AudioWriter] Started: %s (%.0fHz, %dch)\n",
                file.getFullPathName().toRawUTF8(), sampleRate, numChannels);
        return true;
    }

    // Stop writing, flush remaining data, close the file.
    void stopWriting() {
        if (!isThreadRunning()) return;
        shouldStop.store(true);
        stopThread(2000);

        // Final drain
        if (recordFIFO && writer) {
            float buf[4096];
            while (true) {
                int n = recordFIFO->pop(buf, 4096);
                if (n <= 0) break;
                writeInterleavedToFile(buf, n);
            }
        }

        writer.reset();
        recordFIFO = nullptr;
        perfLog("[AudioWriter] Stopped. Total frames: %lld\n", totalSamplesWritten / numChannels);
    }

    int64_t getTotalFramesWritten() const { return totalSamplesWritten / numChannels; }

    // Thread-safe peak data (writer appends, coordinator reads)
    struct PeakEntry { float min; float max; };
    std::vector<PeakEntry> getPeaks() const {
        std::lock_guard<std::mutex> lock(peakMutex);
        return peaks;
    }

private:
    void run() override {
        float buf[4096];
        while (!shouldStop.load()) {
            int n = recordFIFO->pop(buf, 4096);
            if (n > 0) {
                writeInterleavedToFile(buf, n);
            } else {
                juce::Thread::sleep(2);  // brief sleep when FIFO is empty
            }
        }
    }

    void writeInterleavedToFile(const float* interleaved, int numSamples) {
        int numFrames = numSamples / numChannels;
        if (numFrames <= 0) return;

        // De-interleave into channel buffers
        juce::AudioBuffer<float> buffer(numChannels, numFrames);
        for (int frame = 0; frame < numFrames; ++frame) {
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, frame, interleaved[frame * numChannels + ch]);
        }

        writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
        totalSamplesWritten += numSamples;

        // Accumulate peaks (256 samples per peak entry)
        for (int i = 0; i < numFrames; ++i) {
            float sample = 0;
            for (int ch = 0; ch < numChannels; ++ch)
                sample = std::max(sample, std::abs(buffer.getSample(ch, i)));
            peakMin = std::min(peakMin, -sample);
            peakMax = std::max(peakMax, sample);
            if (++peakSampleCount >= 256) {
                std::lock_guard<std::mutex> lock(peakMutex);
                peaks.push_back({ peakMin, peakMax });
                peakMin = 0; peakMax = 0; peakSampleCount = 0;
            }
        }
    }

    AudioRecordFIFO* recordFIFO = nullptr;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    std::atomic<bool> shouldStop { false };
    double sampleRate = 48000;
    int numChannels = 2;
    int64_t totalSamplesWritten = 0;

    // Peak accumulation
    mutable std::mutex peakMutex;
    std::vector<PeakEntry> peaks;
    float peakMin = 0, peakMax = 0;
    int peakSampleCount = 0;
};
