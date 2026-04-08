#include "engine/EngineSync.h"
#include "engine/AudioEngine.h"
#include "engine/Log.h"
#include "api/StateAPI.h"
#include "registry/Registry.h"

// Legacy constructor: subscribes to Registry events
EngineSync::EngineSync(AudioEngine& engine, Registry& registry)
    : engine(engine), registry(&registry) {
    subscriptionId = registry.events().subscribe([this](const RegistryEvent& event) {
        onRegistryEvent(event);
    });
}

// New constructor: subscribes to StateAPI events
EngineSync::EngineSync(AudioEngine& engine, StateAPI& stateAPI)
    : engine(engine), stateAPI(&stateAPI) {
    subscriptionId = stateAPI.events().subscribe([this](const StateEvent& event) {
        onStateEvent(event);
    });
}

EngineSync::~EngineSync() {
    if (subscriptionId >= 0) {
        if (registry)
            registry->events().unsubscribe(subscriptionId);
        else if (stateAPI)
            stateAPI->events().unsubscribe(subscriptionId);
    }
}

// --- StateAPI event handler ---

static std::string entityName(StateEvent::Entity e) {
    switch (e) {
        case StateEvent::Track: return "track";
        case StateEvent::Bus: return "bus";
        case StateEvent::Effect: return "effect";
        case StateEvent::Send: return "send";
        case StateEvent::Song: return "song";
        case StateEvent::Config: return "config";
        default: return "";
    }
}

void EngineSync::onStateEvent(const StateEvent& event) {
    // Song switch via config change
    if (event.entity == StateEvent::Config && event.entityId == "current_song_id") {
        auto newSongId = stateAPI->getMasterOutputId();  // = currentSongId
        if (newSongId != activeSongId) {
            if (newSongId.empty()) {
                clearEngine();
                activeSongId.clear();
            } else {
                loadSong(newSongId);
            }
        }
        return;
    }

    if (activeSongId.empty()) return;

    auto type = entityName(event.entity);

    switch (event.action) {
        case StateEvent::Created:
            if (event.entity == StateEvent::Bus)
                onBusCreated(event.entityId);
            else if (event.entity == StateEvent::Track)
                onTrackCreated(event.entityId);
            else if (event.entity == StateEvent::Effect)
                onEffectCreated(event.entityId);
            else if (event.entity == StateEvent::Send)
                onSendCreated(event.entityId);
            break;

        case StateEvent::Updated:
            onEntityUpdated(type, event.entityId);
            break;

        case StateEvent::Deleted:
            onEntityDeleted(type, event.entityId);
            break;
    }
}

// --- Data access helpers (abstract Registry vs StateAPI) ---

std::vector<EngineSync::TrackData> EngineSync::getTracks() const {
    std::vector<TrackData> result;
    if (stateAPI) {
        auto* song = stateAPI->findSong(activeSongId);
        if (!song) return result;
        for (auto& t : song->tracks)
            result.push_back({ t.id, t.name, t.pluginId, t.presetId, t.outputGain, t.midiEnabled });
    } else if (registry) {
        for (auto& t : registry->tracksForSong(activeSongId))
            result.push_back({ t.id, t.name, t.pluginId, t.presetId, t.outputGain, t.midiEnabled });
    }
    return result;
}

std::vector<EngineSync::BusData> EngineSync::getBusses() const {
    std::vector<BusData> result;
    if (stateAPI) {
        auto* song = stateAPI->findSong(activeSongId);
        if (!song) return result;
        for (auto& b : song->busses)
            result.push_back({ b.id, b.name, b.outputGain });
    } else if (registry) {
        for (auto& b : registry->bussesForSong(activeSongId))
            result.push_back({ b.id, b.name, b.outputGain });
    }
    return result;
}

std::vector<EngineSync::EffectData> EngineSync::getEffectsForParent(const std::string& parentId) const {
    std::vector<EffectData> result;
    if (stateAPI) {
        // Check master, tracks, busses
        auto* song = stateAPI->findSong(activeSongId);
        if (!song) return result;
        const std::vector<EffectState>* list = nullptr;
        if (parentId == activeSongId) list = &song->masterEffects;
        else {
            for (auto& t : song->tracks) if (t.id == parentId) { list = &t.effects; break; }
            if (!list) for (auto& b : song->busses) if (b.id == parentId) { list = &b.effects; break; }
        }
        if (list) for (auto& fx : *list) result.push_back({ fx.id, fx.name, fx.pluginId });
    } else if (registry) {
        for (auto& fx : registry->effectsForParent(parentId))
            result.push_back({ fx.id, fx.name, fx.pluginId });
    }
    return result;
}

std::vector<EngineSync::SendData> EngineSync::getSendsForTrack(const std::string& trackId) const {
    std::vector<SendData> result;
    if (stateAPI) {
        auto* track = stateAPI->findTrack(trackId);
        if (track) for (auto& s : track->sends) result.push_back({ s.id, s.busId, s.gain });
    } else if (registry) {
        for (auto& s : registry->sendsForTrack(trackId))
            result.push_back({ s.id, s.busId, s.gain });
    }
    return result;
}

std::string EngineSync::getPluginName(const std::string& pluginId) const {
    if (stateAPI) {
        auto* p = stateAPI->findPluginById(pluginId);
        return p ? p->name : "";
    } else if (registry) {
        auto p = registry->findPluginById(pluginId);
        return p ? p->name : "";
    }
    return "";
}

float EngineSync::getMasterGain() const {
    if (stateAPI) {
        auto* song = stateAPI->findSong(activeSongId);
        return song ? song->masterGain : 1.0f;
    } else if (registry) {
        return registry->getMasterGain(activeSongId);
    }
    return 1.0f;
}

// --- Core engine operations ---

void EngineSync::clearEngine() {
    engine.clearAllTracks();
    engine.clearAllBusses();
}

void EngineSync::loadSong(const std::string& songId) {
    if (songId.empty()) return;
    activeSongId = songId;

    clearEngine();

    auto busses = getBusses();
    auto tracks = getTracks();
    perfLog("[EngineSync] loadSong: %s (%d tracks, %d busses)\n",
            songId.c_str(), (int)tracks.size(), (int)busses.size());

    // Rebuild in order: busses → tracks → effects → sends
    for (auto& bus : busses) onBusCreated(bus.id);
    for (auto& track : tracks) onTrackCreated(track.id);
    for (auto& bus : busses)
        for (auto& fx : getEffectsForParent(bus.id)) onEffectCreated(fx.id);
    for (auto& track : tracks)
        for (auto& fx : getEffectsForParent(track.id)) onEffectCreated(fx.id);
    for (auto& track : tracks)
        for (auto& send : getSendsForTrack(track.id)) onSendCreated(send.id);

    // Master effects
    for (auto& fx : getEffectsForParent(songId)) onEffectCreated(fx.id);

    engine.setMasterGain(getMasterGain());
}

// --- Individual entity handlers ---

void EngineSync::onBusCreated(const std::string& busId) {
    for (auto& bus : getBusses()) {
        if (bus.id == busId) {
            engine.createBusWithId(juce::String(bus.id), juce::String(bus.name));
            engine.setBusGain(juce::String(bus.id), bus.outputGain);
            return;
        }
    }
}

void EngineSync::onTrackCreated(const std::string& trackId) {
    for (auto& track : getTracks()) {
        if (track.id == trackId) {
            perfLog("[EngineSync] Creating track: %s pluginId=%s\n",
                    track.name.c_str(), track.pluginId.c_str());
            engine.createTrackWithId(juce::String(track.id), juce::String(track.name));
            engine.setTrackGain(juce::String(track.id), track.outputGain);
            if (!track.midiEnabled)
                engine.setTrackMidiEnabled(juce::String(track.id), false);

            if (!track.pluginId.empty()) {
                auto pluginName = getPluginName(track.pluginId);
                if (!pluginName.empty()) {
                    engine.addTrackInstrument(juce::String(track.id), juce::String(pluginName),
                        [id = track.id] {
                            perfLog("[EngineSync] Instrument loaded: %s\n", id.c_str());
                        });
                }
            }
            return;
        }
    }
}

void EngineSync::onEffectCreated(const std::string& effectId) {
    auto searchParents = [&](const std::string& parentId, const juce::String& engineParentId) {
        for (auto& fx : getEffectsForParent(parentId)) {
            if (fx.id == effectId) {
                auto pluginName = getPluginName(fx.pluginId);
                if (!pluginName.empty()) {
                    engine.addEffect(engineParentId,
                                      juce::String(fx.id),
                                      juce::String(pluginName));
                }
                return true;
            }
        }
        return false;
    };

    // Master effects (parent = songId → engine "Output")
    if (searchParents(activeSongId, "Output")) return;

    for (auto& bus : getBusses())
        if (searchParents(bus.id, juce::String(bus.id))) return;
    for (auto& track : getTracks())
        if (searchParents(track.id, juce::String(track.id))) return;
}

void EngineSync::onSendCreated(const std::string& sendId) {
    for (auto& track : getTracks()) {
        for (auto& send : getSendsForTrack(track.id)) {
            if (send.id == sendId) {
                engine.addSend(juce::String(track.id),
                                juce::String(send.busId), send.gain);
                return;
            }
        }
    }
}

void EngineSync::onEntityUpdated(const std::string& entityType, const std::string& entityId) {
    auto id = juce::String(entityId);

    if (entityType == "track") {
        for (auto& t : getTracks()) {
            if (t.id == entityId) {
                engine.setTrackGain(id, t.outputGain);
                engine.setTrackMidiEnabled(id, t.midiEnabled);
                engine.renameTrack(id, juce::String(t.name));

                // Detect instrument change
                auto currentPlugin = engine.getTrackPluginName(id);
                if (!t.pluginId.empty()) {
                    auto pluginName = getPluginName(t.pluginId);
                    if (!pluginName.empty() && juce::String(pluginName) != currentPlugin) {
                        if (currentPlugin.isNotEmpty())
                            engine.removeTrackInstrument(id);
                        engine.addTrackInstrument(id, juce::String(pluginName),
                            [trackId = t.id] {
                                perfLog("[EngineSync] Instrument loaded: %s\n", trackId.c_str());
                            });
                    }
                } else if (currentPlugin.isNotEmpty()) {
                    engine.removeTrackInstrument(id);
                }
                return;
            }
        }
    }
    else if (entityType == "bus") {
        for (auto& b : getBusses()) {
            if (b.id == entityId) {
                engine.setBusGain(id, b.outputGain);
                engine.renameBus(id, juce::String(b.name));
                return;
            }
        }
    }
    else if (entityType == "send") {
        for (auto& t : getTracks()) {
            for (auto& s : getSendsForTrack(t.id)) {
                if (s.id == entityId) {
                    engine.setSendGain(juce::String(t.id), juce::String(s.busId), s.gain);
                    return;
                }
            }
        }
    }
    else if (entityType == "song") {
        if (entityId == activeSongId)
            engine.setMasterGain(getMasterGain());
    }
}

void EngineSync::onEntityDeleted(const std::string& entityType, const std::string& entityId) {
    auto id = juce::String(entityId);
    if (entityType == "track")
        engine.removeTrack(id);
    else if (entityType == "bus")
        engine.removeBus(id);
    else if (entityType == "effect") {
        // Try all parents including master output
        engine.removeEffect(juce::String("Output"), id);
        for (auto& t : getTracks())
            engine.removeEffect(juce::String(t.id), id);
        for (auto& b : getBusses())
            engine.removeEffect(juce::String(b.id), id);
    }
}

// --- Legacy event dispatch ---

void EngineSync::onRegistryEvent(const RegistryEvent& event) {
    if (event.entityType == "config" && event.entityId == "current_song_id") {
        auto newSongId = registry->getConfig("current_song_id");
        if (newSongId != activeSongId) {
            if (newSongId.empty()) {
                clearEngine();
                activeSongId.clear();
            } else {
                loadSong(newSongId);
            }
        }
        return;
    }

    if (activeSongId.empty()) return;

    switch (event.action) {
        case RegistryEvent::Created:
            if (event.entityType == EntityType::Bus)
                onBusCreated(event.entityId);
            else if (event.entityType == EntityType::Track)
                onTrackCreated(event.entityId);
            else if (event.entityType == EntityType::Effect)
                onEffectCreated(event.entityId);
            else if (event.entityType == EntityType::Send)
                onSendCreated(event.entityId);
            break;

        case RegistryEvent::Updated:
            onEntityUpdated(event.entityType, event.entityId);
            break;

        case RegistryEvent::Deleted:
            onEntityDeleted(event.entityType, event.entityId);
            break;
    }
}
