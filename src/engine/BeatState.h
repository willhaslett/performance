#pragma once

#include "state/StateModel.h"
#include <atomic>
#include <memory>
#include <vector>

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

    // Tempo-map-aware commit. Checks the tempo map for events whose
    // beat falls in [window.prevBeat, window.nextBeat) and, if any
    // crossed, sets tempo to the LAST one's bpm and reanchors at
    // window.nextBeat. Otherwise advances continuously like
    // commitContinuous. The (α) accuracy contract is: the new tempo
    // takes effect at the NEXT buffer boundary (up to one buffer
    // length of drift per tempo event — see docs/UNIFIED_EVENTS.md).
    // (β) would replace this with segment-emitting begin/commit; the
    // tempo-map storage and crossed-event detection stay identical.
    void commitBlock(int numSamples, const Window& window) {
        // libc++ on macOS doesn't yet support std::atomic<std::shared_ptr<T>>
        // (requires trivially-copyable T). Use the legacy
        // std::atomic_load_explicit free function — deprecated in C++20
        // but functional and widely used until atomic<shared_ptr> lands.
        auto map = std::atomic_load_explicit(&tempoMap, std::memory_order_acquire);
        if (map && !map->empty()) {
            double newBpm = 0.0;
            bool crossed = false;
            // "Crossed in this buffer" = event beat strictly AFTER
            // prevBeat (not already-applied) and BEFORE nextBeat
            // (still within this window). Events at exact prevBeat
            // are already in effect — their bpm seeded the live
            // tempo via setTempoMap.
            for (auto& e : *map) {
                if (e.beat > window.prevBeat + 1e-9
                    && e.beat < window.nextBeat - 1e-9) {
                    newBpm = e.bpm;
                    crossed = true;
                }
            }
            if (crossed) {
                tempo.store(newBpm, std::memory_order_release);
                commitReanchored(window.nextBeat);
                lastCrossedBpm.store(newBpm, std::memory_order_relaxed);
                crossCount.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        commitContinuous(numSamples, window.nextBeat);
    }

    // --- Audio-thread → message-thread diagnostics (temporary) ---
    // Bumped on each tempo-event crossing detected in commitBlock.
    // Lets the message-thread poll figure out whether the audio path
    // actually sees + acts on tempo events.
    int  readCrossCount()  const { return crossCount.load(std::memory_order_relaxed); }
    double readLastCrossedBpm() const { return lastCrossedBpm.load(std::memory_order_relaxed); }
    int  readMapSize() const {
        auto map = std::atomic_load_explicit(&tempoMap, std::memory_order_acquire);
        return map ? static_cast<int>(map->size()) : 0;
    }

    // Push the project's tempo events from the message thread. Audio
    // thread reads via `commitBlock` to detect crossings. Empty vector
    // disables tempo-map handling and falls back to constant `tempo`.
    void setTempoMap(const std::vector<TempoEvent>& events) {
        auto copy = std::make_shared<const std::vector<TempoEvent>>(events);
        std::atomic_store_explicit(&tempoMap, copy, std::memory_order_release);
        // Seed the live tempo atomic with the most-recent-prior-or-equal
        // event at the current beat position so the FIRST buffer after
        // setTempoMap uses the right initial tempo. Without this seeding,
        // commitBlock's strict-greater check wouldn't fire on a beat-0
        // event when starting from beat 0 — the initial tempo would
        // remain whatever setTempo had set most recently.
        if (events.empty()) return;
        double current = beatPosition.load(std::memory_order_acquire);
        double bpm = events.front().bpm;
        for (auto& e : events) {
            if (e.beat > current + 1e-9) break;
            bpm = e.bpm;
        }
        tempo.store(bpm, std::memory_order_release);
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
    // Multi-event tempo map (audio-thread visible). Empty / nullptr
    // = no tempo events; commitBlock falls back to commitContinuous.
    // Accessed via the legacy std::atomic_load/_store free functions
    // because libc++ on macOS doesn't yet support
    // std::atomic<std::shared_ptr<T>> (requires trivially-copyable T).
    std::shared_ptr<const std::vector<TempoEvent>> tempoMap;
    // Diagnostics (audio-thread writes, message-thread reads).
    std::atomic<int> crossCount { 0 };
    std::atomic<double> lastCrossedBpm { 0.0 };
};
