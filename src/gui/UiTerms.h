#pragma once
#include <juce_core/juce_core.h>

// User-facing terminology for the app's top-level document unit.
//
// The internal code uses "Song" (SongId, SongState, loadSong(), etc.)
// and that is not going to change — too much surface area, and the
// internal stability is a feature for Lua bindings and the composer
// integration's future eyes on the codebase.
//
// What *is* up for experimentation is the word the user sees in the
// UI. "Song" fits a single self-contained musical idea someone plays
// live. "Piece" has compositional connotations. "Project" reads as
// "a container for work I'm doing." We don't know which is best yet.
//
// To experiment: edit the four values below and rebuild. Nothing else
// in the codebase needs to change.
//
// Call sites:
//   "New " + UiTerms::docSingular          — menu items, buttons
//   UiTerms::docPlural                      — section headers
//   "Loading " + UiTerms::docSingularLower — overlay / status text
//   "throughout the " + UiTerms::docSingularLower — inline prose

namespace UiTerms {

inline juce::String docSingular      = "Song";
inline juce::String docPlural        = "Songs";
inline juce::String docSingularLower = "song";
inline juce::String docPluralLower   = "songs";

}  // namespace UiTerms
