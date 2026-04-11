#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "daw/Arrangement.h"
#include "state/StateModel.h"
#include "engine/MidiSourceNode.h"
#include "engine/AudioFileNode.h"
#include "engine/RecordFIFO.h"
#include "engine/AudioRecordFIFO.h"
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
    void setPlaying(bool p) {
        bool wasPlaying = playing.exchange(p, std::memory_order_release);
        if (wasPlaying && !p)
            flushAllNotes();
    }
    void setTempo(double bpm) { tempo.store(bpm, std::memory_order_release); }
    void setBeatPosition(double beat) {
        bool isPlaying = playing.load(std::memory_order_acquire);
        if (isPlaying)
            flushAllNotes();
        beatPosition.store(beat, std::memory_order_release);
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
    AudioRecordFIFO& getAudioRecordFIFO() { return audioRecordFIFO; }

    // Set audio input channels to capture when recording (for armed audio tracks)
    void setAudioRecordChannels(int startChannel, int channelCount) {
        audioRecordChStart.store(startChannel, std::memory_order_release);
        audioRecordChCount.store(channelCount, std::memory_order_release);
    }

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

    // --- Per-track audio file playback ---
    void registerTrackAudioFileNode(const juce::String& trackId, AudioFileNode* node) {
        trackAudioFileNodes[trackId] = node;
    }
    void unregisterTrackAudioFileNode(const juce::String& trackId) {
        trackAudioFileNodes.erase(trackId);
    }
    void clearTrackAudioFileNodes() {
        trackAudioFileNodes.clear();
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
                    [&](const std::string& trackId, const MidiEventState& event, double eventBeat) {
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

                // Drive audio file nodes — check which regions cover prevBeat
                for (auto& [trkId, afNode] : trackAudioFileNodes) {
                    if (!afNode || !afNode->hasFile()) {
                        if (afNode) afNode->setActive(false);
                        continue;
                    }
                    // Check if any audio region on this track covers prevBeat
                    auto regions = arr->regionsForTrack(trkId.toStdString());
                    bool found = false;
                    for (auto* r : regions) {
                        if (r->type != "audio") continue;
                        double endBeat = r->startBeat + r->lengthBeats;
                        if (prevBeat >= r->startBeat && prevBeat < endBeat) {
                            afNode->setRegionStartBeat(r->startBeat);
                            afNode->setPlaybackBeat(prevBeat);
                            afNode->setActive(true);
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        afNode->setActive(false);
                }
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

            // Capture audio input for recording (armed audio tracks)
            if (recording.load(std::memory_order_acquire)) {
                int chStart = audioRecordChStart.load(std::memory_order_acquire);
                int chCount = audioRecordChCount.load(std::memory_order_acquire);
                if (chStart >= 0 && chCount > 0 && chStart + chCount <= buffer.getNumChannels()) {
                    int frames = buffer.getNumSamples();
                    // Interleave into scratch buffer (max 2048 frames * 2 channels)
                    int total = frames * chCount;
                    if (total <= audioScratchSize) {
                        for (int f = 0; f < frames; ++f)
                            for (int c = 0; c < chCount; ++c)
                                audioScratch[f * chCount + c] = buffer.getSample(chStart + c, f);
                        audioRecordFIFO.push(audioScratch, total);
                    }
                }
            }
        }

        // Flush all notes and deactivate audio nodes if requested (stop/seek)
        if (needsNoteFlush.exchange(false, std::memory_order_acquire)) {
            for (auto& [trackId, node] : trackMidiSources) {
                if (!node) continue;
                for (int ch = 1; ch <= 16; ++ch) {
                    node->scheduleSingleMessage(juce::MidiMessage::allNotesOff(ch), 0);
                    node->scheduleSingleMessage(juce::MidiMessage::allSoundOff(ch), 0);
                }
            }
            for (auto& [trackId, afNode] : trackAudioFileNodes) {
                if (afNode) afNode->setActive(false);
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
    AudioRecordFIFO audioRecordFIFO;
    std::atomic<int> audioRecordChStart { -1 };
    std::atomic<int> audioRecordChCount { 0 };
    static constexpr int audioScratchSize = 4096;  // 2048 frames * 2 channels
    float audioScratch[audioScratchSize];

    // Track MIDI sources — modified only during rebuildConnections (not while processing)
    std::map<juce::String, MidiSourceNode*> trackMidiSources;
    std::map<juce::String, AudioFileNode*> trackAudioFileNodes;

    // Flag: flush all notes on next processBlock
    std::atomic<bool> needsNoteFlush { false };

    void flushAllNotes() {
        needsNoteFlush.store(true, std::memory_order_release);
    }
};
