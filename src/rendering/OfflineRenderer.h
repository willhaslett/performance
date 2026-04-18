#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <functional>

class AudioEngine;
class GraphWrapper;

// OfflineRenderer — drives the audio graph faster-than-realtime and writes
// the master output to a WAV file. Synchronous render on a caller-owned
// background thread. The engine's device callback must be paused for the
// duration (see AudioEngine::pauseDeviceProcessing).
//
// Spike-level limitations (tracked in CLAUDE.md §4):
//   - Constant tempo: the render uses the tempo at the moment render() is
//     called and applies it uniformly. TempoMap is a prerequisite for
//     variable tempo.
//   - Automation engine is expected to be paused by the caller.
//   - No tail-time: hard cutoff at endBeat.
//   - Master output only. No stem bouncing.
//   - Plugin non-realtime compatibility varies per plugin. We call
//     setNonRealtime(true) on every processor in the graph before render
//     and restore it after.

class OfflineRenderer {
public:
    struct Result {
        bool ok = false;
        juce::String errorMessage;
        double wallClockSeconds = 0.0;   // render wall-clock time
        double audioDurationSeconds = 0.0; // resulting audio length
        int64_t framesWritten = 0;
    };

    static Result render(AudioEngine& engine,
                         const juce::File& outputFile,
                         double startBeat,
                         double endBeat,
                         double tempoBpm);
};
