#pragma once

#include <juce_core/juce_core.h>
#include <string>
#include <vector>

// Emits ABC notation text from a collection of notes per voice + project
// metadata. Inverse of ABCParser.
//
// House style (locked):
//   - Header order: X: T: L: Q: M: K:
//   - X always 1, L always 1/8, K always emitted (defaults to "none").
//   - Pitch convention: ABC standard (C = MIDI 60 / middle C, c = 72,
//     c' = 84, C, = 48). Accidentals always explicit (^C / _C); never
//     rely on bar-implicit naturals.
//   - Bar lines every measure per M:. Four bars per line.
//   - Notes that cross bar lines split with tie (`C2- | C2`).
//   - Chords (`[CEG]`) used when notes in one voice share both
//     startBeat and durationBeats.
//   - Multi-voice: V: declarations in header, one block per voice.
//   - Drums: voice name "Drums" emits a `%%MIDI drummap` directive +
//     short letter macros (B=kick S=snare H=hhc O=hho R=ride C=crash
//     T=ltom M=mtom A=htom).
struct ABCWriteInput {
    struct Note {
        double startBeat = 0.0;     // beats from start of the piece
        double durationBeats = 0.0;
        int pitch = 60;             // MIDI 0-127
        int velocity = 80;          // 1-127 (currently informational; ABC dynamics not emitted in V1)
    };

    // Optional region-segmentation marker. When a voice has segments,
    // each segment's notes are emitted preceded by `P:LABEL` so callers
    // (getTrack / getProject) can mark region boundaries in the output.
    // The label is conventionally `B<beat>` (e.g., `B0`, `B16`) — the
    // same beat the LLM would pass to getRegion(track, beat).
    struct Segment {
        std::string label;            // e.g., "B0", "B16"
        std::vector<Note> notes;      // beats are piece-absolute (same as Voice::notes)
    };

    struct Voice {
        std::string name;             // "Piano", "Drums", ...
        bool isDrums = false;         // emits %%MIDI drummap + drum-letter macros
        std::vector<Note> notes;      // single-segment shorthand (no P: marker emitted)
        std::vector<Segment> segments;// multi-segment with P: labels — when non-empty,
                                      // takes precedence over `notes`
    };

    double tempo = 120.0;
    int timeSignatureNum = 4;
    int timeSignatureDen = 4;
    std::string key = "none";       // "C", "Am", "F#mix", "none"
    std::string title;              // optional; emitted as T: if non-empty

    std::vector<Voice> voices;

    // Total length of the piece in beats. Writer emits enough bars to
    // cover this, padding voices with rests as needed. If 0, length is
    // inferred from the latest note across all voices.
    double lengthBeats = 0.0;
};

class ABCWriter {
public:
    // Write the input as ABC text. Returns the ABC string. Never fails
    // (malformed inputs produce best-effort output rather than throwing).
    std::string write(const ABCWriteInput& input);
};
