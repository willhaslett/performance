#pragma once
#include "state/StateModel.h"
#include <functional>
#include <string>
#include <vector>

class StateAPI;

// ActionRefs — utilities for finding and removing action-instance dependencies
// on entities (tracks, busses, presets). Used by the cascade-delete confirmation
// flow when the user removes an entity that has action events or bindings
// referencing it, and by the load-time repair dialog that finds bindings
// whose refs can no longer be resolved.
//
// The walk uses the typed ActionInfo.params schema to know which arg positions
// are refs — no string-matching heuristics.
namespace ActionRefs {

struct DependentCount {
    int actionEvents = 0;  // on any action track in any song
    int bindings     = 0;  // song-scoped + global
};

DependentCount countDependents(const StateAPI& state, const std::string& entityId);

// Remove every action event and binding that references the entity. Does not
// remove the entity itself — caller does that after this returns.
void removeDependents(StateAPI& state, const std::string& entityId);

// Description of one instance whose args reference a now-missing entity.
struct StaleRef {
    std::string summary;            // human-readable ("Binding 'Pad 1' → Fade out → <missing track>")
    std::function<void()> remove;   // invoke to delete just this instance
};

// Scan every binding and action event; for each arg whose ParamSchema declares
// it as a ChannelRef / PresetRef, verify the ref resolves. Returns a list of
// stale instances with per-instance remove callbacks for the repair dialog.
std::vector<StaleRef> findStaleRefs(StateAPI& state);

}  // namespace ActionRefs
