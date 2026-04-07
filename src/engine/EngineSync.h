#pragma once
#include <juce_events/juce_events.h>
#include "registry/RegistryEvents.h"
#include <string>

class AudioEngine;
class Registry;

// Pure event subscriber — keeps the AudioEngine in sync with the registry.
// No public methods. Subscribes to registry events and reacts.
// The API never calls this directly — it just writes to the registry.
class EngineSync {
public:
    EngineSync(AudioEngine& engine, Registry& registry);
    ~EngineSync();

private:
    void onRegistryEvent(const RegistryEvent& event);

    // Song lifecycle
    void loadSong(const std::string& songId);
    void clearEngine();

    // Individual entity handlers (used by events and loadSong)
    void onBusCreated(const std::string& busId);
    void onTrackCreated(const std::string& trackId);
    void onEffectCreated(const std::string& effectId);
    void onSendCreated(const std::string& sendId);
    void onEntityUpdated(const std::string& entityType, const std::string& entityId);
    void onEntityDeleted(const std::string& entityType, const std::string& entityId);

    AudioEngine& engine;
    Registry& registry;
    std::string activeSongId;
    int subscriptionId = -1;
};
