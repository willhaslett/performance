#pragma once
#include <juce_core/juce_core.h>

// Identity for a single installation of Performance on a user's macOS
// account. Persisted at ~/Library/Application Support/com.performance.app/
// install.json so it survives state.db resets and version upgrades but
// gets a fresh ID on a new machine, a new user account, or a manual wipe
// of the Application Support directory — all of which legitimately
// represent a new installation.
//
// First read lazily generates and persists the ID; subsequent reads are
// cached.
namespace InstallId {
    juce::String id();         // UUID
    juce::String firstSeen();  // ISO-8601 timestamp of first-ever launch
}
