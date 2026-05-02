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

    // --- Transport command (atomic double-buffer, message thread → audio thread) ---
    struct TransportState {
        bool playing = false;
        double bpm = 120.0;
        double beat = 0.0;
        bool loopEnabled = false;
        double loopStart = 0.0;
        double loopEnd = 0.0;
    };

    // Atomic start: sets position, tempo, loop, and playing in one shot
    void startPlayback(double beat, double bpm, bool loopOn, double loopS, double loopE) {
        auto& buf = transportBuf[1 - transportActive.load(std::memory_order_acquire)];
        buf.playing = true;
        buf.bpm = bpm;
        buf.beat = beat;
        buf.loopEnabled = loopOn;
        buf.loopStart = loopS;
        buf.loopEnd = loopE;
        transportSeq.fetch_add(1, std::memory_order_release);
        transportActive.store(1 - transportActive.load(std::memory_order_acquire), std::memory_order_release);
    }

    void stopPlayback() {
        auto& buf = transportBuf[1 - transportActive.load(std::memory_order_acquire)];
        buf = transportBuf[transportActive.load(std::memory_order_acquire)];  // copy current
        buf.playing = false;
        transportSeq.fetch_add(1, std::memory_order_release);
        transportActive.store(1 - transportActive.load(std::memory_order_acquire), std::memory_order_release);
        flushAllNotes();
    }

    // Legacy individual setters (used for tempo/loop updates during playback)
    // These do NOT reset position — only startPlayback/stopPlayback do that.
    void setPlaying(bool p) {
        if (!p) { stopPlayback(); return; }
        playing.store(true, std::memory_order_release);
    }
    void setTempo(double bpm) { tempo.store(bpm, std::memory_order_release); }
    void setBeatPosition(double beat) {
        bool isPlaying = playing.load(std::memory_order_acquire);
        if (isPlaying) flushAllNotes();
        beatPosition.store(beat, std::memory_order_release);
        samplesSinceStart.store(0, std::memory_order_release);
        baseBeat.store(beat, std::memory_order_release);
    }

    double getBeatPosition() const { return beatPosition.load(std::memory_order_acquire); }

    void setLoop(bool enabled, double start, double end) {
        loopEnabled.store(enabled, std::memory_order_release);
        loopStart.store(start, std::memory_order_release);
        loopEnd.store(end, std::memory_order_release);
    }

    // --- Arrangement (set from message thread) ---
    void setArrangement(const Arrangement* arr) {
        arrangement.store(arr, std::memory_order_release);
    }

    // --- Metronome ---
    void setMetronome(bool on) { metronomeOn.store(on, std::memory_order_release); }
    void setBeatsPerBar(int bpb) { metronomeBPB.store(bpb, std::memory_order_release); }
    void setMetronomeVolume(float vol) { metronomeVol.store(vol, std::memory_order_release); }

    // --- Recording ---
    void setRecording(bool r) { recording.store(r, std::memory_order_release); }
    bool isRecording() const { return recording.load(std::memory_order_acquire); }
    RecordFIFO& getRecordFIFO() { return recordFIFO; }

    // --- Recording diagnostics (see PerformanceCoordinator timer log) ---
    // Written in the audio thread while recording=true, read from the
    // message thread. No behavior — just counters to pinpoint where
    // events are being lost on a broken record path.
    struct RecordDiag {
        int processBlockTicks;
        int midiEventsSeen;
        int fifoPushes;
        int playingTicks;           // how many of those ticks had playing=true
    };
    RecordDiag readRecordDiag() const {
        return {
            diagProcessBlockTicks.load(std::memory_order_acquire),
            diagMidiEventsSeen.load(std::memory_order_acquire),
            diagFifoPushes.load(std::memory_order_acquire),
            diagPlayingTicks.load(std::memory_order_acquire),
        };
    }
    void resetRecordDiag() {
        diagProcessBlockTicks.store(0);
        diagMidiEventsSeen.store(0);
        diagFifoPushes.store(0);
        diagPlayingTicks.store(0);
    }
    // Per-track audio recording
    struct AudioRecordTarget {
        int chStart = -1;
        int chCount = 0;
        AudioRecordFIFO* fifo = nullptr;
    };
    void setAudioRecordTargets(const std::vector<AudioRecordTarget>& targets) {
        audioRecordTargets = targets;
    }
    void clearAudioRecordTargets() {
        audioRecordTargets.clear();
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
        // Diagnostic: count every audio-callback tick while recording=true,
        // regardless of the playing gate. Cheap; only relaxed atomics.
        if (recording.load(std::memory_order_acquire)) {
            diagProcessBlockTicks.fetch_add(1, std::memory_order_relaxed);
            if (playing.load(std::memory_order_acquire))
                diagPlayingTicks.fetch_add(1, std::memory_order_relaxed);
            int blockMidi = 0;
            for (const auto metadata : midi) { (void) metadata; ++blockMidi; }
            if (blockMidi > 0)
                diagMidiEventsSeen.fetch_add(blockMidi, std::memory_order_relaxed);
        }
        // Check for new atomic transport command
        int64_t seq = transportSeq.load(std::memory_order_acquire);
        if (seq != lastTransportSeq) {
            lastTransportSeq = seq;
            auto& cmd = transportBuf[transportActive.load(std::memory_order_acquire)];
            playing.store(cmd.playing, std::memory_order_release);
            tempo.store(cmd.bpm, std::memory_order_release);
            loopEnabled.store(cmd.loopEnabled, std::memory_order_release);
            loopStart.store(cmd.loopStart, std::memory_order_release);
            loopEnd.store(cmd.loopEnd, std::memory_order_release);
            if (cmd.playing) {
                beatPosition.store(cmd.beat, std::memory_order_release);
                baseBeat.store(cmd.beat, std::memory_order_release);
                samplesSinceStart.store(0, std::memory_order_release);
            }
        }

        if (playing.load(std::memory_order_acquire) && currentSampleRate > 0) {
            double bpm = tempo.load(std::memory_order_acquire);
            double base = baseBeat.load(std::memory_order_acquire);
            int64_t samples = samplesSinceStart.load(std::memory_order_acquire);

            int numSamples = buffer.getNumSamples();
            double beatsPerSample = (bpm / 60.0) / currentSampleRate;

            double prevBeat = base + samples * beatsPerSample;
            double nextBeat = base + (samples + numSamples) * beatsPerSample;

            // Loop wrapping
            if (loopEnabled.load(std::memory_order_acquire)) {
                double lEnd = loopEnd.load(std::memory_order_acquire);
                double lStart = loopStart.load(std::memory_order_acquire);
                if (lEnd > lStart && nextBeat >= lEnd) {
                    double offset = nextBeat - lEnd;
                    double loopLen = lEnd - lStart;
                    nextBeat = lStart + std::fmod(offset, loopLen);
                    prevBeat = nextBeat - numSamples * beatsPerSample;
                    if (prevBeat < lStart) prevBeat = lStart;
                    // Reset sample counter to match new position
                    baseBeat.store(nextBeat, std::memory_order_release);
                    samplesSinceStart.store(0, std::memory_order_release);
                    // Flush active notes NOW — before the scan picks up new notes.
                    // Only flush via MidiSourceNodes (sequencer path), NOT the live
                    // midi buffer — a live-path noteOff can arrive after the new noteOn
                    // from the scan, killing it.
                    for (int ch = 0; ch < 16; ++ch) {
                        auto& ns = activeNotes[ch];
                        for (int note = 0; note < 128; ++note) {
                            bool on = (note < 64) ? (ns.lo & (1ULL << note)) : (ns.hi & (1ULL << (note - 64)));
                            if (!on) continue;
                            auto msg = juce::MidiMessage::noteOff(ch + 1, note);
                            for (auto& [tid, node] : trackMidiSources)
                                if (node) node->scheduleSingleMessage(msg, 0);
                        }
                        ns.clearAll();
                    }
                }
            }

            // Update running position
            samplesSinceStart.store(
                loopEnabled.load(std::memory_order_acquire)
                    ? samplesSinceStart.load(std::memory_order_acquire) + numSamples
                    : samples + numSamples,
                std::memory_order_release);
            beatPosition.store(nextBeat, std::memory_order_release);

            // Scan arrangement and schedule per-track MIDI
            auto* arr = arrangement.load(std::memory_order_acquire);
            if (arr) {
                arr->scanMidiEvents(prevBeat, nextBeat,
                    [&](const TrackId& trackId, const MidiEventState& event, double eventBeat) {
                        auto it = trackMidiSources.find(juce::String(trackId.str()));
                        if (it == trackMidiSources.end() || !it->second) return;

                        int sampleOffset = (beatsPerSample > 0)
                            ? juce::jlimit(0, numSamples - 1,
                                  (int)((eventBeat - prevBeat) / beatsPerSample))
                            : 0;

                        auto msg = juce::MidiMessage(event.status | (event.channel - 1),
                                                      event.data1, event.data2);
                        it->second->scheduleSingleMessage(msg, sampleOffset);

                        // Track active notes for targeted flush
                        int ch = event.channel - 1;
                        if (ch >= 0 && ch < 16 && event.data1 < 128) {
                            if ((event.status & 0xF0) == 0x90 && event.data2 > 0)
                                activeNotes[ch].set(event.data1);
                            else if ((event.status & 0xF0) == 0x80
                                  || ((event.status & 0xF0) == 0x90 && event.data2 == 0))
                                activeNotes[ch].clear(event.data1);
                        }
                    });

                // Drive audio file nodes from the active pool — arrangement
                // regions when in Arrangement mode, the track's loop region
                // when in Looper mode (with modular wrap by lengthBeats).
                // Without the mode gate, switching from Looper to Arrangement
                // mid-playback would leak Producer audio that happened to
                // sit under the wrapped playhead.
                const bool looperActive = arr->isLooperModeActive();
                for (auto& [trkId, afNode] : trackAudioFileNodes) {
                    if (!afNode || !afNode->hasFiles()) {
                        if (afNode) afNode->setActive(false);
                        continue;
                    }

                    if (looperActive) {
                        auto* loop = arr->loopForTrack(TrackId{trkId.toStdString()});
                        if (loop && loop->type == "audio" && ! loop->muted
                            && loop->lengthBeats > 0.0) {
                            // Wrap the playhead within the loop (sequencer
                            // already wraps the master cycle; if the loop
                            // length matches master this is a no-op, but
                            // sub-master loops still play correctly).
                            double pos = std::fmod(prevBeat, loop->lengthBeats);
                            if (pos < 0.0) pos += loop->lengthBeats;
                            afNode->setActiveRegion(juce::String(loop->id.str()),
                                                     0.0, pos);
                        } else {
                            afNode->setActive(false);
                        }
                        continue;
                    }

                    auto regions = arr->regionsForTrack(TrackId{trkId.toStdString()});
                    bool found = false;
                    for (auto* r : regions) {
                        if (r->type != "audio" || r->muted) continue;
                        double endBeat = r->startBeat + r->lengthBeats;
                        if (prevBeat >= r->startBeat && prevBeat < endBeat) {
                            afNode->setActiveRegion(juce::String(r->id.str()), r->startBeat, prevBeat);
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
                        diagFifoPushes.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }

            // Capture audio input for recording (all armed audio tracks)
            if (recording.load(std::memory_order_acquire)) {
                for (auto& target : audioRecordTargets) {
                    if (target.chStart < 0 || target.chCount <= 0 || !target.fifo) continue;
                    if (target.chStart + target.chCount > buffer.getNumChannels()) continue;
                    int frames = buffer.getNumSamples();
                    int total = frames * target.chCount;
                    if (total <= audioScratchSize) {
                        for (int f = 0; f < frames; ++f)
                            for (int c = 0; c < target.chCount; ++c)
                                audioScratch[f * target.chCount + c] = buffer.getSample(target.chStart + c, f);
                        target.fifo->push(audioScratch, total);
                    }
                }
            }
        }

        // Track live MIDI input notes for flush
        for (const auto metadata : midi) {
            auto msg = metadata.getMessage();
            int ch = msg.getChannel() - 1;
            if (ch >= 0 && ch < 16) {
                if (msg.isNoteOn()) activeNotes[ch].set(msg.getNoteNumber());
                else if (msg.isNoteOff()) activeNotes[ch].clear(msg.getNoteNumber());
            }
        }

        // Flush notes if requested (stop/seek/loop)
        // OUTSIDE the playing check — must fire even after setPlaying(false)
        if (needsNoteFlush.exchange(false, std::memory_order_acquire)) {
            bool isStopped = !playing.load(std::memory_order_acquire);
            // Send explicit noteOff for each tracked active note
            for (int ch = 0; ch < 16; ++ch) {
                auto& ns = activeNotes[ch];
                for (int note = 0; note < 128; ++note) {
                    bool active = (note < 64) ? (ns.lo & (1ULL << note)) : (ns.hi & (1ULL << (note - 64)));
                    if (!active) continue;
                    auto msg = juce::MidiMessage::noteOff(ch + 1, note);
                    for (auto& [trackId, node] : trackMidiSources)
                        if (node) node->scheduleSingleMessage(msg, 0);
                    midi.addEvent(msg, 0);
                }
                ns.clearAll();
            }
            if (isStopped) {
                for (auto& [trackId, afNode] : trackAudioFileNodes)
                    if (afNode) afNode->setActive(false);
            }
        }

        // Forward to the graph
        graph.processBlock(buffer, midi);

        // Metronome click — mix into output after graph processing
        if (metronomeOn.load(std::memory_order_acquire)
            && playing.load(std::memory_order_acquire) && currentSampleRate > 0) {
            double bpm = tempo.load(std::memory_order_acquire);
            double base = baseBeat.load(std::memory_order_acquire);
            int64_t samples = samplesSinceStart.load(std::memory_order_acquire);
            int numSamples = buffer.getNumSamples();
            double beatsPerSample = (bpm / 60.0) / currentSampleRate;
            double prevBeat = base + (samples - numSamples) * beatsPerSample;
            double nextBeat = base + samples * beatsPerSample;
            int bpb = metronomeBPB.load(std::memory_order_acquire);
            float vol = metronomeVol.load(std::memory_order_acquire);

            // Find the next integer beat at or after prevBeat
            int nextIntBeat = (int)std::ceil(prevBeat - 0.0001);  // small epsilon for floating point
            if ((double)nextIntBeat >= prevBeat && (double)nextIntBeat < nextBeat) {
                double beatBoundary = (double)nextIntBeat;
                int clickOffset = (int)((beatBoundary - prevBeat) / beatsPerSample);
                clickOffset = juce::jlimit(0, numSamples - 1, clickOffset);

                bool accent = (bpb > 0 && nextIntBeat % bpb == 0);
                float freq = accent ? 1000.0f : 700.0f;
                float clickGain = (accent ? 0.4f : 0.25f) * vol;
                int clickLen = (int)(currentSampleRate * 0.015);

                for (int i = 0; i < clickLen && (clickOffset + i) < numSamples; ++i) {
                    float env = 1.0f - (float)i / clickLen;
                    float sample = std::sin(2.0f * juce::MathConstants<float>::pi * freq * i / (float)currentSampleRate)
                                   * env * clickGain;
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        buffer.addSample(ch, clickOffset + i, sample);
                }
            }
        }
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

    // Transport — double-buffered command + legacy atomics
    TransportState transportBuf[2];
    std::atomic<int> transportActive { 0 };
    std::atomic<int64_t> transportSeq { 0 };
    int64_t lastTransportSeq = 0;

    // Legacy atomics (still used for per-buffer position tracking)
    std::atomic<bool> playing { false };
    std::atomic<double> tempo { 120.0 };
    std::atomic<double> beatPosition { 0.0 };
    std::atomic<double> baseBeat { 0.0 };
    std::atomic<int64_t> samplesSinceStart { 0 };
    double currentSampleRate = 0;

    // Loop
    std::atomic<bool> loopEnabled { false };
    std::atomic<double> loopStart { 0.0 };
    std::atomic<double> loopEnd { 0.0 };

    // Arrangement pointer (atomic — swapped from message thread)
    std::atomic<const Arrangement*> arrangement { nullptr };

    // Metronome
    std::atomic<bool> metronomeOn { false };
    std::atomic<int> metronomeBPB { 4 };
    std::atomic<float> metronomeVol { 0.5f };

    // Recording
    std::atomic<bool> recording { false };
    RecordFIFO recordFIFO;
    // Diagnostic counters — audio-thread writes, message-thread reads.
    std::atomic<int> diagProcessBlockTicks { 0 };
    std::atomic<int> diagMidiEventsSeen    { 0 };
    std::atomic<int> diagFifoPushes        { 0 };
    std::atomic<int> diagPlayingTicks      { 0 };
    std::vector<AudioRecordTarget> audioRecordTargets;
    static constexpr int audioScratchSize = 4096;
    float audioScratch[audioScratchSize];

    // Track MIDI sources — modified only during rebuildConnections (not while processing)
    std::map<juce::String, MidiSourceNode*> trackMidiSources;
    std::map<juce::String, AudioFileNode*> trackAudioFileNodes;

    // Active note tracking for targeted flush (audio thread only)
    // Bit set: activeNotes[channel-1] has bit N set if note N is on
    // Two uint64s per channel cover 128 notes
    struct NoteSet {
        uint64_t lo = 0, hi = 0;  // bits 0-63, 64-127
        void set(int n) { if (n < 64) lo |= (1ULL << n); else hi |= (1ULL << (n - 64)); }
        void clear(int n) { if (n < 64) lo &= ~(1ULL << n); else hi &= ~(1ULL << (n - 64)); }
        void clearAll() { lo = hi = 0; }
    };
    NoteSet activeNotes[16];  // per MIDI channel

    // Flag: flush all notes on next processBlock
    std::atomic<bool> needsNoteFlush { false };

    void flushAllNotes() {
        needsNoteFlush.store(true, std::memory_order_release);
    }
};
