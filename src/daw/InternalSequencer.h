#pragma once
#include "daw/SequencerAPI.h"
#include "state/StateModel.h"
#include <atomic>
#include <mutex>
#include <vector>

// InternalSequencer — our own transport and beat clock.
// No external DAW dependency. Driven by a high-resolution timer.
// Thread-safe: audio thread reads position, message thread controls transport.

class InternalSequencer : public SequencerAPI {
public:
    InternalSequencer();
    ~InternalSequencer() override;

    // --- Transport ---
    void play() override;
    void stop() override;
    void togglePlayStop() override;
    bool isPlaying() const override;

    // --- Tempo ---
    void setTempo(double bpm) override;
    double getTempo() const override;

    // --- Position ---
    double getBeatPosition() const override;
    void setBeatPosition(double beat) override;

    // --- Time signature ---
    void setTimeSignature(int numerator, int denominator) override;
    int getTimeSignatureNumerator() const override;
    int getTimeSignatureDenominator() const override;

    // --- Loop ---
    void setLoopEnabled(bool enabled) override;
    bool isLoopEnabled() const override;
    void setLoopRange(double startBeat, double endBeat) override;
    double getLoopStart() const override;
    double getLoopEnd() const override;

    // --- Metronome ---
    void setMetronomeEnabled(bool enabled) override;
    bool isMetronomeEnabled() const override;

    // --- Callbacks ---
    void setBeatCallback(BeatCallback callback) override;
    void setTransportCallback(TransportCallback callback) override;

    // --- Capabilities ---
    Capabilities getCapabilities() const override;

    // Called from a timer or audio callback to advance the clock.
    // deltaSeconds = time since last call.
    //
    // When tempoEvents is non-empty (set via setTempoEvents) the
    // advance walks event boundaries between prevBeat and the new
    // position, switching BPM as it crosses each. The atomic `tempo`
    // is kept up to date with the currently-effective BPM so audio-
    // thread reads of getTempo() see the right value at every tick.
    void advance(double deltaSeconds);

    // Push the full song-level tempo map into the sequencer. Empty
    // vector falls back to the legacy single-`tempo` behavior.
    // Thread-safety: writer = message thread; reader = message
    // thread inside advance(). Behind a mutex; not on the audio
    // critical path.
    void setTempoEvents(const std::vector<TempoEvent>& events);

    // Set position without triggering beat callbacks (for sync from audio thread)
    void setBeatPositionSilent(double beat);

private:
    std::atomic<bool> playing { false };
    std::atomic<double> tempo { 120.0 };
    std::atomic<double> beatPosition { 0.0 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<double> loopStart { 0.0 };
    std::atomic<double> loopEnd { 0.0 };  // 0 = no cycle set
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<int> timeSigNum { 4 };
    std::atomic<int> timeSigDen { 4 };

    std::mutex callbackMutex;
    BeatCallback beatCallback;
    TransportCallback transportCallback;

    // Multi-event tempo map. Empty = use the atomic `tempo` directly
    // (legacy single-tempo behavior). Locked under tempoMapMutex; only
    // touched by message-thread code (writer = setTempoEvents,
    // reader = advance which runs on the timer thread).
    std::mutex tempoMapMutex;
    std::vector<TempoEvent> tempoEvents;

    double lastBeatNotified = -1.0;
};
