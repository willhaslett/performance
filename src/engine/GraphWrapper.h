#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "daw/Arrangement.h"
#include "state/StateModel.h"
#include "engine/MidiSourceNode.h"
#include "engine/RecordFIFO.h"
#include <atomic>
#include <map>

// Wraps an AudioProcessorGraph so we can do per-buffer work (MIDI scheduling)
// before the graph processes. Connected to AudioProcessorPlayer in place of
// the raw graph.
//
// The audio thread calls processBlock, which:
//   1. Advances the sample-accurate beat clock
//   2. Scans the Arrangement for MIDI events in this buffer's beat range
//   3. Routes events to per-track MidiSourceNodes with sample-accurate offsets
//   4. Delegates to the graph's processBlock

class GraphWrapper : public juce::AudioProcessor {
public:
    explicit GraphWrapper(juce::AudioProcessorGraph& graph)
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo())
              .withOutput("Output", juce::AudioChannelSet::stereo())),
          graph(graph) {}

    // --- Transport state (set from message thread, read on audio thread) ---
    void setPlaying(bool p) { playing.store(p, std::memory_order_release); }
    void setTempo(double bpm) { tempo.store(bpm, std::memory_order_release); }
    void setBeatPosition(double beat) {
        beatPosition.store(beat, std::memory_order_release);
        // Reset sample counter so next processBlock starts from this beat
        samplesSinceStart.store(0, std::memory_order_release);
        baseBeat.store(beat, std::memory_order_release);
    }

    double getBeatPosition() const { return beatPosition.load(std::memory_order_acquire); }

    // --- Arrangement (set from message thread) ---
    void setArrangement(const Arrangement* arr) {
        arrangement.store(arr, std::memory_order_release);
    }

    // --- Recording ---
    void setRecording(bool r) { recording.store(r, std::memory_order_release); }
    bool isRecording() const { return recording.load(std::memory_order_acquire); }
    RecordFIFO& getRecordFIFO() { return recordFIFO; }

    // --- Per-track MIDI source registration ---
    // Called from AudioEngine when tracks are created/destroyed (message thread,
    // but only when graph is not processing — during rebuildConnections)
    void registerTrackMidiSource(const juce::String& trackId, MidiSourceNode* node) {
        trackMidiSources[trackId] = node;
    }
    void unregisterTrackMidiSource(const juce::String& trackId) {
        trackMidiSources.erase(trackId);
    }
    void clearTrackMidiSources() {
        trackMidiSources.clear();
    }

    // --- AudioProcessor overrides ---
    void prepareToPlay(double sampleRate, int blockSize) override {
        currentSampleRate = sampleRate;
        graph.setPlayConfigDetails(
            getTotalNumInputChannels(), getTotalNumOutputChannels(),
            sampleRate, blockSize);
        graph.prepareToPlay(sampleRate, blockSize);
    }

    void releaseResources() override {
        graph.releaseResources();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        if (playing.load(std::memory_order_acquire) && currentSampleRate > 0) {
            double bpm = tempo.load(std::memory_order_acquire);
            double base = baseBeat.load(std::memory_order_acquire);
            int64_t samples = samplesSinceStart.load(std::memory_order_acquire);

            int numSamples = buffer.getNumSamples();
            double beatsPerSample = (bpm / 60.0) / currentSampleRate;

            double prevBeat = base + samples * beatsPerSample;
            double nextBeat = base + (samples + numSamples) * beatsPerSample;

            // Update running position
            samplesSinceStart.store(samples + numSamples, std::memory_order_release);
            beatPosition.store(nextBeat, std::memory_order_release);

            // Scan arrangement and schedule per-track MIDI
            auto* arr = arrangement.load(std::memory_order_acquire);
            if (arr) {
                arr->scanMidiEvents(prevBeat, nextBeat,
                    [&](const std::string& trackId, const RegionState::Event& event, double eventBeat) {
                        auto it = trackMidiSources.find(juce::String(trackId));
                        if (it == trackMidiSources.end() || !it->second) return;

                        int sampleOffset = (beatsPerSample > 0)
                            ? juce::jlimit(0, numSamples - 1,
                                  (int)((eventBeat - prevBeat) / beatsPerSample))
                            : 0;

                        auto msg = juce::MidiMessage(event.status | (event.channel - 1),
                                                      event.data1, event.data2);
                        it->second->scheduleSingleMessage(msg, sampleOffset);
                    });
            }

            // Capture incoming live MIDI for recording (all event types)
            if (recording.load(std::memory_order_acquire)) {
                for (const auto metadata : midi) {
                    auto msg = metadata.getMessage();
                    if (msg.isNoteOnOrOff() || msg.isController() ||
                        msg.isChannelPressure() || msg.isAftertouch() ||
                        msg.isPitchWheel() || msg.isProgramChange()) {
                        double eventBeat = prevBeat + metadata.samplePosition * beatsPerSample;
                        recordFIFO.push({ msg.getRawData()[0] & 0xF0,
                                          msg.getRawDataSize() > 1 ? msg.getRawData()[1] : 0,
                                          msg.getRawDataSize() > 2 ? msg.getRawData()[2] : 0,
                                          msg.getChannel(), eventBeat });
                    }
                }
            }
        }

        // Forward to the graph
        graph.processBlock(buffer, midi);
    }

    const juce::String getName() const override { return "GraphWrapper"; }
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
    juce::AudioProcessorGraph& graph;

    // Transport (atomics — written by message thread, read by audio thread)
    std::atomic<bool> playing { false };
    std::atomic<double> tempo { 120.0 };
    std::atomic<double> beatPosition { 0.0 };
    std::atomic<double> baseBeat { 0.0 };
    std::atomic<int64_t> samplesSinceStart { 0 };
    double currentSampleRate = 0;

    // Arrangement pointer (atomic — swapped from message thread)
    std::atomic<const Arrangement*> arrangement { nullptr };

    // Recording
    std::atomic<bool> recording { false };
    RecordFIFO recordFIFO;

    // Track MIDI sources — modified only during rebuildConnections (not while processing)
    std::map<juce::String, MidiSourceNode*> trackMidiSources;
};
