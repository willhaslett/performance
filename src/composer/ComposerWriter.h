#pragma once

#include "composer/ComposerOutput.h"
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

class PerformanceCoordinator;

// Applies a ComposerOutput to the current project: creates regions
// on each named track and populates them with MIDI events via
// StateAPI + Arrangement. All mutations flow through the normal
// event bus so autosave, undo, and engine sync all work.
//
// Semantics:
//   - One MIDI region per named track per apply() call, each
//     starting at the same project-time beat.
//   - Unknown track names cause apply() to return false with an
//     explanatory error. Regions created before the error are
//     left in place; undo rolls them back.
//   - Tempo / time signature in the output are currently ignored;
//     the project's existing settings are used. Revisit if we
//     want compose-mode to be able to change tempo mid-project.
class ComposerWriter {
public:
    explicit ComposerWriter(PerformanceCoordinator& coord) : coord(coord) {}

    bool apply(const ComposerOutput& output,
               double startBeat,
               std::string& err);

private:
    PerformanceCoordinator& coord;
};
