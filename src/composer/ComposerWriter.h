#pragma once

#include "composer/ComposerOutput.h"
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

class StateAPI;

// Applies a ComposerOutput to the current project: creates regions
// on each named track and populates them with MIDI events. Pure
// StateAPI writes — all mutations flow through the normal event bus
// and so autosave, undo, and engine sync all work automatically.
//
// Semantics:
//   - One region per named track per apply() call.
//   - startBeat is the project-time position of the first bar.
//   - Unknown track names cause apply() to return false with an
//     explanatory error; partial state from earlier tracks in the
//     same call is left in place (caller's undo undoes everything
//     in one step because StateAPI coalesces within a call).
//   - Tempo / time signature in the output are currently ignored;
//     the project's existing settings are used. Revisit if we
//     want compose-mode to be able to change tempo mid-project.
class ComposerWriter {
public:
    explicit ComposerWriter(StateAPI& state) : state(state) {}

    bool apply(const ComposerOutput& output,
               double startBeat,
               std::string& err);

private:
    StateAPI& state;
};
