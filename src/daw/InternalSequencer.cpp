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

    double bpm = tempo.load(std::memory_order_relaxed);
    double beatsPerSecond = bpm / 60.0;
    double advance = deltaSeconds * beatsPerSecond;

    double pos = beatPosition.load(std::memory_order_relaxed) + advance;

    // Loop
    if (loopEnabled.load(std::memory_order_relaxed)) {
        double end = loopEnd.load(std::memory_order_relaxed);
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
        if (beatCallback) beatCallback(pos, bpm);
    }
}
