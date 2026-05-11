#pragma once

#include <atomic>

// Beat-tracking state for the audio thread. Owns the four atomics that
// the per-buffer beat math depends on:
//   - tempo            : current effective BPM
//   - baseBeat         : the beat at samplesSinceStart == 0
//   - samplesSinceStart: samples elapsed from baseBeat at current tempo
//   - beatPosition     : last-block-end beat (for getter queries)
//
// The point of having a class wrap these is to make the coupling
// EXPLICIT. Before this extraction, the four atomics were scattered
// across GraphWrapper::processBlock with stores at multiple points.
// Each extension required understanding implicit coupling rules ("the
// second store at line 286 uses a stale local var; the loop-wrap store
// at line 265 must precede it; ..."). That implicit coupling produced
// bug-after-bug (see docs/UNIFIED_EVENTS.md, Phase E2 rationale).
//
// All mutation goes through the methods here. processBlock calls
// beginBlock() once (read-only), then endBlock() once (atomic update).
// Direct setters exist for transport commands from the message thread
// (setTempo / resetPosition / etc.) but they go through the same class
// so the coupling stays in one file.
//
// Thread-safety: all atomics are `relaxed`-friendly (the audio thread
// reads, the message thread writes). The class doesn't take locks.
class BeatState {
public:
    // Snapshot returned by beginBlock. The caller uses prevBeat /
    // nextBeat for the current buffer's MIDI scan + audio-region
    // start computation. beatsPerSample is exposed because callers
    // need it to convert eventBeat → sampleOffset.
    struct Window {
        double prevBeat = 0.0;
        double nextBeat = 0.0;
        double beatsPerSample = 0.0;
    };

    // Read the current state and compute this buffer's [prevBeat, nextBeat]
    // window. Does not mutate. Safe to call from the audio thread.
    Window beginBlock(int numSamples, double sampleRate) const {
        double bpm   = tempo.load(std::memory_order_acquire);
        double base  = baseBeat.load(std::memory_order_acquire);
        long long s  = samplesSinceStart.load(std::memory_order_acquire);
        Window w;
        w.beatsPerSample = (sampleRate > 0) ? (bpm / 60.0) / sampleRate : 0.0;
        w.prevBeat = base + static_cast<double>(s) * w.beatsPerSample;
        w.nextBeat = base + static_cast<double>(s + numSamples) * w.beatsPerSample;
        return w;
    }

    // Commit the buffer's progress. Two paths:
    //   - Continuous: buffer played through without a position jump.
    //     samplesSinceStart accumulates; baseBeat unchanged.
    //   - Re-anchored: buffer ended at a position not equal to the
    //     continuous-extrapolation nextBeat (loop wrap, or in E3 a
    //     mid-buffer tempo change). Reset baseBeat and samplesSinceStart
    //     to the new anchor.
    //
    // This is the ONE place the three coupled atomics get updated
    // together, eliminating the line-286-clobbers-line-265 trap that
    // plagued the previous design.
    void commitContinuous(int numSamples, double newBeatPosition) {
        samplesSinceStart.fetch_add(numSamples, std::memory_order_acq_rel);
        beatPosition.store(newBeatPosition, std::memory_order_release);
    }

    void commitReanchored(double newBaseBeat) {
        baseBeat.store(newBaseBeat, std::memory_order_release);
        samplesSinceStart.store(0, std::memory_order_release);
        beatPosition.store(newBaseBeat, std::memory_order_release);
    }

    // --- Direct setters (message thread) ---

    void setTempo(double bpm) {
        tempo.store(bpm, std::memory_order_release);
    }

    double getTempo() const {
        return tempo.load(std::memory_order_acquire);
    }

    // Reset the beat anchor to a specific position. Equivalent to
    // commitReanchored but exposed by name for transport setters.
    void resetPosition(double beat) {
        commitReanchored(beat);
    }

    // Atomic transport command: set position + tempo together (used by
    // startPlayback). Same as resetPosition + setTempo but the order
    // matters less because they're already separate atomics; this is
    // for documentation.
    void resetTransport(double beat, double bpm) {
        tempo.store(bpm, std::memory_order_release);
        commitReanchored(beat);
    }

    double getBeatPosition() const {
        return beatPosition.load(std::memory_order_acquire);
    }

private:
    std::atomic<double> tempo            { 120.0 };
    std::atomic<double> baseBeat         { 0.0   };
    std::atomic<long long> samplesSinceStart { 0 };
    std::atomic<double> beatPosition     { 0.0   };
};
