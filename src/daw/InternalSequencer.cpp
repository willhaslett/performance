#include "daw/InternalSequencer.h"
#include <cmath>

InternalSequencer::InternalSequencer() {}

InternalSequencer::~InternalSequencer() {
    stop();
}

void InternalSequencer::play() {
    if (!playing.exchange(true)) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (transportCallback) transportCallback(true);
    }
}

void InternalSequencer::stop() {
    if (playing.exchange(false)) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (transportCallback) transportCallback(false);
    }
}

void InternalSequencer::togglePlayStop() {
    if (isPlaying()) stop(); else play();
}

bool InternalSequencer::isPlaying() const {
    return playing.load(std::memory_order_relaxed);
}

void InternalSequencer::setTempo(double bpm) {
    tempo.store(std::max(20.0, std::min(300.0, bpm)), std::memory_order_relaxed);
}

double InternalSequencer::getTempo() const {
    return tempo.load(std::memory_order_relaxed);
}

double InternalSequencer::getBeatPosition() const {
    return beatPosition.load(std::memory_order_relaxed);
}

void InternalSequencer::setBeatPosition(double beat) {
    beatPosition.store(std::max(0.0, beat), std::memory_order_relaxed);
    lastBeatNotified = -1.0;  // force re-notify
}

void InternalSequencer::setBeatPositionSilent(double beat) {
    beatPosition.store(std::max(0.0, beat), std::memory_order_relaxed);
}

void InternalSequencer::setTimeSignature(int numerator, int denominator) {
    timeSigNum.store(numerator);
    timeSigDen.store(denominator);
}

int InternalSequencer::getTimeSignatureNumerator() const {
    return timeSigNum.load();
}

int InternalSequencer::getTimeSignatureDenominator() const {
    return timeSigDen.load();
}

void InternalSequencer::setLoopEnabled(bool enabled) {
    loopEnabled.store(enabled, std::memory_order_relaxed);
}

bool InternalSequencer::isLoopEnabled() const {
    return loopEnabled.load(std::memory_order_relaxed);
}

void InternalSequencer::setLoopRange(double startBeat, double endBeat) {
    loopStart.store(startBeat, std::memory_order_relaxed);
    loopEnd.store(endBeat, std::memory_order_relaxed);
}

double InternalSequencer::getLoopStart() const {
    return loopStart.load(std::memory_order_relaxed);
}

double InternalSequencer::getLoopEnd() const {
    return loopEnd.load(std::memory_order_relaxed);
}

void InternalSequencer::setMetronomeEnabled(bool enabled) {
    metronomeEnabled.store(enabled, std::memory_order_relaxed);
}

bool InternalSequencer::isMetronomeEnabled() const {
    return metronomeEnabled.load(std::memory_order_relaxed);
}

void InternalSequencer::setBeatCallback(BeatCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    beatCallback = std::move(callback);
}

void InternalSequencer::setTransportCallback(TransportCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    transportCallback = std::move(callback);
}

SequencerAPI::Capabilities InternalSequencer::getCapabilities() const {
    return {
        .hasTransport = true,
        .hasTempo = true,
        .hasLoop = true,
        .hasMetronome = true,
        .hasRecording = false,
        .hasClipTrigger = false,
        .hasExternalSync = false,
    };
}

void InternalSequencer::advance(double deltaSeconds) {
    if (!playing.load(std::memory_order_relaxed)) return;

    double pos = beatPosition.load(std::memory_order_relaxed);

    // Snapshot tempo events under the lock so the walk below operates on
    // a stable copy. Vector is small (typically 1-10 entries) so the
    // copy cost is negligible.
    std::vector<TempoEvent> events;
    {
        std::lock_guard<std::mutex> lock(tempoMapMutex);
        events = tempoEvents;
    }

    auto bpmAtBeat = [&events, this](double beat) -> double {
        if (events.empty()) return tempo.load(std::memory_order_relaxed);
        double bpm = events.front().bpm;
        for (auto& e : events) {
            if (e.beat > beat + 1e-9) break;
            bpm = e.bpm;
        }
        return bpm;
    };

    auto nextEventAfter = [&events](double beat) -> const TempoEvent* {
        for (auto& e : events) {
            if (e.beat > beat + 1e-9) return &e;
        }
        return nullptr;
    };

    double remaining = deltaSeconds;
    double currentBpm = bpmAtBeat(pos);

    // Walk segments: at each step, advance until we hit the next
    // tempo event or run out of time, whichever comes first.
    while (remaining > 0.0) {
        double secondsPerBeat = 60.0 / currentBpm;
        const TempoEvent* next = nextEventAfter(pos);
        if (!next) {
            pos += remaining / secondsPerBeat;
            break;
        }
        double beatsToNext   = next->beat - pos;
        double secondsToNext = beatsToNext * secondsPerBeat;
        if (secondsToNext > remaining) {
            pos += remaining / secondsPerBeat;
            break;
        }
        pos        = next->beat;
        remaining -= secondsToNext;
        currentBpm = next->bpm;
    }

    // Push the now-effective BPM into the atomic so audio-thread
    // readers (e.g., future PositionInfo population for plugins) see
    // the right value.
    tempo.store(currentBpm, std::memory_order_relaxed);

    // Loop wrap. NOTE: a tempo change inside the loop range will lose
    // continuity at wrap (we re-compute tempo from the wrapped position).
    // Acceptable V1 limitation; document if a tester trips on it.
    if (loopEnabled.load(std::memory_order_relaxed)) {
        double end   = loopEnd.load(std::memory_order_relaxed);
        double start = loopStart.load(std::memory_order_relaxed);
        if (pos >= end && end > start)
            pos = start + std::fmod(pos - start, end - start);
    }

    beatPosition.store(pos, std::memory_order_relaxed);

    // Beat callback — fire on each new integer beat
    double currentBeat = std::floor(pos);
    if (currentBeat > lastBeatNotified) {
        lastBeatNotified = currentBeat;
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (beatCallback) beatCallback(pos, currentBpm);
    }
}

void InternalSequencer::setTempoEvents(const std::vector<TempoEvent>& events) {
    std::lock_guard<std::mutex> lock(tempoMapMutex);
    tempoEvents = events;
    // Keep the atomic in sync with event[0] for getTempo() correctness
    // when transport is stopped (advance() doesn't run, so the tempo
    // atomic wouldn't otherwise reflect updates).
    if (!tempoEvents.empty())
        tempo.store(tempoEvents.front().bpm, std::memory_order_relaxed);
}
