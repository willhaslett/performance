#pragma once

#include "composer/NotationParser.h"

// dawai's v2 notation — bar-grouped multitrack text format.
//
// Grammar (short version; full spec + prompt in ~/ideas_and_projects/dawai):
//
//   header:
//     tempo <bpm>
//     time <n/d>
//     key <root> <mode>
//     feel <description>             (swing / shuffle triggers triplet feel)
//     track <name> = <gm program>    (or "drums")
//
//   body (per bar):
//     bar <N> | <Chord>
//     <track_name>: beat <pos> <note_or_chord> <duration> <velocity> | ...
//
// Durations: w, h, h., q, q., 8th, 8th., 16th
// Velocity:  ppp pp p mp mf f ff fff  (or 1-127)
// Chords:    [C4 E4 G4]
// Rests:     `r`
// Drums:     named hits (kick, snare, hhc, ...) on tracks declared as "drums"
class V2NotationParser : public NotationParser {
public:
    bool parse(const juce::String& input,
               ComposerOutput& out,
               std::string& err) override;
};
