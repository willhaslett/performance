#pragma once
#include <juce_events/juce_events.h>
#include "state/StateEvents.h"
#include "state/Id.h"
#include <string>

class AudioEngineInterface;
class StateAPI;

// Pure event subscriber — keeps the AudioEngine in sync with state.
// No public methods. Subscribes to StateAPI events and reacts.
class EngineSync {
public:
    EngineSync(AudioEngineInterface& engine, StateAPI& stateAPI);
    ~EngineSync();

private:
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

    // Restore plugin state from preset file after async load
    void restorePresetState(const std::string& parentId, const std::string& effectId,
                            const PresetId& presetId);

    AudioEngineInterface& engine;
    StateAPI& stateAPI;
    std::string activeSongId;
    int subscriptionId = -1;
};
