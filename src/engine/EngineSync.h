#pragma once
#include <string>
#include <set>

class AudioEngine;
class Registry;

// Syncs the AudioEngine to match the registry state for a given song.
// The registry is the source of truth; the engine is a view of it.
// Call sync() after any registry mutation. It's idempotent.
class EngineSync {
public:
    EngineSync(AudioEngine& engine, Registry& registry);

    // Sync engine state to match the given song in the registry
    void sync(const std::string& songId);

    // Clear all engine state
    void clear();

private:
    AudioEngine& engine;
    Registry& registry;

    // Track what's currently in the engine to diff against
    std::set<std::string> engineTrackNames;
    std::set<std::string> engineBusNames;
    std::set<std::string> engineEffectIds;
    std::set<std::string> engineSendIds;
};
