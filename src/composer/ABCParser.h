#pragma once

#include "composer/NotationParser.h"

// ABC notation parser. Implements `NotationParser` so it can plug into
// the same composer pipeline as V2NotationParser.
//
// Subset supported (V1 — what the LLM actually emits):
//   - Headers: X: T: L: Q: M: K: (and K:none)
//   - Voice declarations (V:Name) and per-voice content blocks
//   - Notes: pitch letters, ^ / _ / = accidentals, octave markers (, ')
//   - Chords: [CEG] — same start, same duration
//   - Rests: z (and Z for whole-bar rests)
//   - Durations: bare letter (= L:), `n` multipliers, `/n` divisors,
//     `n/m` fractions
//   - Ties: trailing `-` on a note
//   - Bar lines: |, |], |:, :|, :: (basic acknowledgment)
//   - Repeats: |: ... :|, [1 ... :| [2 ... :|  — expanded inline
//   - %%MIDI drummap directive (defines per-letter drum pitches)
//   - Drum letters defined via drummap on a voice marked as drums
//   - Comments: lines starting with `%` (other than `%%` directives)
//
// Explicitly rejected (returns error so material isn't silently lost):
//   - Inline tempo / meter changes ([Q:...], [M:...]) — Phase 3 concern
//   - Mid-piece K: changes (key changes)
//   - Macros (m:)
//   - Decorations like !ff! — silently skipped, no velocity mapping in V1
class ABCParser : public NotationParser {
public:
    bool parse(const juce::String& input,
               ComposerOutput& out,
               std::string& err) override;
};
