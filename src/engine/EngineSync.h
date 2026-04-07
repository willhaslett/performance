#pragma once
#include <juce_events/juce_events.h>
#include "registry/RegistryEvents.h"
#include "state/StateEvents.h"
#include <string>

class AudioEngine;
class Registry;
class StateAPI;

// Pure event subscriber — keeps the AudioEngine in sync with state.
// No public methods. Subscribes to state events and reacts.
class EngineSync {
public:
    // Legacy: subscribes to Registry events
    EngineSync(AudioEngine& engine, Registry& registry);
    // New: subscribes to StateAPI events
    EngineSync(AudioEngine& engine, StateAPI& stateAPI);
    ~EngineSync();

private:
    void onRegistryEvent(const RegistryEvent& event);
    void onStateEvent(const StateEvent& event);

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

    // Data access — abstracts Registry vs StateAPI
    bool usesStateAPI() const { return stateAPI != nullptr; }

    // Track/bus/effect/send queries that work with either backend
    struct TrackData { std::string id; std::string name; std::string pluginId; std::string presetId;
                       float outputGain; bool midiEnabled; };
    struct BusData { std::string id; std::string name; float outputGain; };
    struct EffectData { std::string id; std::string name; std::string pluginId; };
    struct SendData { std::string id; std::string busId; float gain; };

    std::vector<TrackData> getTracks() const;
    std::vector<BusData> getBusses() const;
    std::vector<EffectData> getEffectsForParent(const std::string& parentId) const;
    std::vector<SendData> getSendsForTrack(const std::string& trackId) const;
    std::string getPluginName(const std::string& pluginId) const;
    float getMasterGain() const;

    AudioEngine& engine;
    Registry* registry = nullptr;      // legacy mode
    StateAPI* stateAPI = nullptr;      // new mode
    std::string activeSongId;
    int subscriptionId = -1;
};
