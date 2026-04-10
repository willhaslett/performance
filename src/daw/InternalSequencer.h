#pragma once
#include "daw/SequencerAPI.h"
#include <atomic>
#include <mutex>

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
    void advance(double deltaSeconds);

    // Set position without triggering beat callbacks (for sync from audio thread)
    void setBeatPositionSilent(double beat);

private:
    std::atomic<bool> playing { false };
    std::atomic<double> tempo { 120.0 };
    std::atomic<double> beatPosition { 0.0 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<double> loopStart { 0.0 };
    std::atomic<double> loopEnd { 4.0 };
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<int> timeSigNum { 4 };
    std::atomic<int> timeSigDen { 4 };

    std::mutex callbackMutex;
    BeatCallback beatCallback;
    TransportCallback transportCallback;

    double lastBeatNotified = -1.0;
};
