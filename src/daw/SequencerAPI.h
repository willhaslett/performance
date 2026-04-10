#pragma once
#include <functional>
#include <string>

// SequencerAPI — abstract interface for transport and sequencing.
//
// All sequencer functionality goes through this interface. The app can
// function fully without a sequencer attached (nullptr checks or no-op
// default). Implementations: InternalSequencer (our own), or future
// DAW bridges (MCU, OSC, Link, etc).
//
// Design constraints:
// - No dependencies on StateAPI, AudioEngine, or GUI
// - Implementations live in src/daw/
// - The coordinator owns the sequencer and wires it to state/engine
// - If the sequencer is null or disabled, the app works as before

class SequencerAPI {
public:
    virtual ~SequencerAPI() = default;

    // --- Transport ---
    virtual void play() = 0;
    virtual void stop() = 0;
    virtual void togglePlayStop() = 0;
    virtual bool isPlaying() const = 0;

    // --- Tempo ---
    virtual void setTempo(double bpm) = 0;
    virtual double getTempo() const = 0;

    // --- Position ---
    // Beat position: 0.0 = start, 1.0 = beat 2, etc.
    virtual double getBeatPosition() const = 0;
    virtual void setBeatPosition(double beat) = 0;

    // --- Time signature ---
    virtual void setTimeSignature(int numerator, int denominator) = 0;
    virtual int getTimeSignatureNumerator() const = 0;
    virtual int getTimeSignatureDenominator() const = 0;

    // --- Loop ---
    virtual void setLoopEnabled(bool enabled) = 0;
    virtual bool isLoopEnabled() const = 0;
    virtual void setLoopRange(double startBeat, double endBeat) = 0;
    virtual double getLoopStart() const = 0;
    virtual double getLoopEnd() const = 0;

    // --- Metronome ---
    virtual void setMetronomeEnabled(bool enabled) = 0;
    virtual bool isMetronomeEnabled() const = 0;

    // --- Recording (future) ---
    // virtual void startRecording() = 0;
    // virtual void stopRecording() = 0;
    // virtual bool isRecording() const = 0;

    // --- Callbacks ---
    // Called on each beat (or sub-beat) from the audio thread or timer.
    // Listeners use this for beat-synced visual updates.
    using BeatCallback = std::function<void(double beatPosition, double bpm)>;
    virtual void setBeatCallback(BeatCallback callback) = 0;

    // Called on transport state change (play/stop)
    using TransportCallback = std::function<void(bool isPlaying)>;
    virtual void setTransportCallback(TransportCallback callback) = 0;

    // --- Capabilities ---
    // Implementations declare what they support. Callers check before calling.
    struct Capabilities {
        bool hasTransport = true;
        bool hasTempo = true;
        bool hasLoop = false;
        bool hasMetronome = false;
        bool hasRecording = false;
        bool hasClipTrigger = false;  // future: Ableton/Bitwig only
        bool hasExternalSync = false; // future: Link
    };
    virtual Capabilities getCapabilities() const = 0;
};
