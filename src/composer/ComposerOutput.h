#pragma once

#include <juce_core/juce_core.h>
#include <string>
#include <utility>
#include <vector>

// Format-independent output of any NotationParser.
//
// Every parser emits this struct; ComposerWriter consumes it.
// Keeping the surface flat (no nested regions, no structural
// metadata beyond tempo / time signature) makes it easy to add
// a new parser — it just needs to produce these notes.
struct ComposerOutput {
    double tempo = 120.0;
    // Optional overrides. Empty vector = use the project's current signature.
    std::vector<std::pair<int, int>> timeSignature;

    struct Note {
        std::string trackName;     // the LLM writes by name; resolved to UUID by writer
        double startBeat = 0.0;    // beats from the region start (0-based)
        double durationBeats = 0.0;
        int pitch = 60;            // MIDI 0-127
        float velocity = 0.8f;     // 0-1 (writer clamps to valid MIDI range)
    };
    std::vector<Note> notes;

    // Length of the region to create for each named track, in beats.
    // Parsers compute this from the notation's bar count * beats-per-bar.
    double lengthBeats = 0.0;
};
