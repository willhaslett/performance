#pragma once
#include <juce_events/juce_events.h>
#include <string>
#include <set>

class AudioEngine;
class Registry;

// Syncs the AudioEngine to match the registry state for a given song.
// The registry is the source of truth; the engine is a view of it.
// Call sync() after any structural registry mutation. It's idempotent.
// Continuous values (gain, etc.) use targeted engine updates, not sync.
class EngineSync {
public:
    EngineSync(AudioEngine& engine, Registry& registry);

    // Sync engine state to match the given song in the registry
    void sync(const std::string& songId);

    // Set the active song ID
    void setActiveSong(const std::string& songId) { activeSongId = songId; }

    // Notify that a track or bus was removed (clears from tracking sets)
    void notifyRemoved(const std::string& name);
    void notifyRenamed(const std::string& oldName, const std::string& newName);

    // Clear all engine state
    void clear();

private:
    AudioEngine& engine;
    Registry& registry;
    std::string activeSongId;

    // Track what's currently in the engine to diff against
    std::set<std::string> engineTrackNames;
    std::set<std::string> engineBusNames;
    std::set<std::string> engineEffectIds;
    std::set<std::string> engineSendIds;
};
