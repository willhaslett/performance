#include "engine/EngineSync.h"
#include "engine/AudioEngine.h"
#include "engine/Log.h"
#include "registry/Registry.h"

EngineSync::EngineSync(AudioEngine& engine, Registry& registry)
    : engine(engine), registry(registry) {
    subscriptionId = registry.events().subscribe([this](const RegistryEvent& event) {
        onRegistryEvent(event);
    });
}

EngineSync::~EngineSync() {
    if (subscriptionId >= 0)
        registry.events().unsubscribe(subscriptionId);
}

void EngineSync::clearEngine() {
    engine.clearAllTracks();
    engine.clearAllBusses();
}

void EngineSync::loadSong(const std::string& songId) {
    if (songId.empty()) return;
    activeSongId = songId;

    clearEngine();

    // Rebuild in order: busses → tracks → effects → sends
    for (auto& bus : registry.bussesForSong(songId))
        onBusCreated(bus.id);
    for (auto& track : registry.tracksForSong(songId))
        onTrackCreated(track.id);
    for (auto& bus : registry.bussesForSong(songId))
        for (auto& fx : registry.effectsForParent(bus.id))
            onEffectCreated(fx.id);
    for (auto& track : registry.tracksForSong(songId))
        for (auto& fx : registry.effectsForParent(track.id))
            onEffectCreated(fx.id);
    for (auto& track : registry.tracksForSong(songId))
        for (auto& send : registry.sendsForTrack(track.id))
            onSendCreated(send.id);

    // Master effects (parent = songId)
    for (auto& fx : registry.effectsForParent(songId))
        onEffectCreated(fx.id);

    engine.setMasterGain(registry.getMasterGain(songId));
}

// --- Individual entity handlers ---

void EngineSync::onBusCreated(const std::string& busId) {
    for (auto& bus : registry.bussesForSong(activeSongId)) {
        if (bus.id == busId) {
            engine.createBusWithId(juce::String(bus.id), juce::String(bus.name));
            engine.setBusGain(juce::String(bus.id), bus.outputGain);
            return;
        }
    }
}

void EngineSync::onTrackCreated(const std::string& trackId) {
    for (auto& track : registry.tracksForSong(activeSongId)) {
        if (track.id == trackId) {
            engine.createTrackWithId(juce::String(track.id), juce::String(track.name));
            engine.setTrackGain(juce::String(track.id), track.outputGain);
            if (!track.midiEnabled)
                engine.setTrackMidiEnabled(juce::String(track.id), false);

            if (!track.pluginId.empty()) {
                auto plugin = registry.findPluginById(track.pluginId);
                if (plugin) {
                    engine.addTrackInstrument(juce::String(track.id), juce::String(plugin->name),
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
        for (auto& fx : registry.effectsForParent(parentId)) {
            if (fx.id == effectId) {
                auto plugin = registry.findPluginById(fx.pluginId);
                if (plugin) {
                    engine.addEffect(engineParentId,
                                      juce::String(fx.id),
                                      juce::String(plugin->name));
                }
                return true;
            }
        }
        return false;
    };

    // Master effects (parent = songId → engine "Output")
    if (searchParents(activeSongId, "Output")) return;

    for (auto& bus : registry.bussesForSong(activeSongId))
        if (searchParents(bus.id, juce::String(bus.id))) return;
    for (auto& track : registry.tracksForSong(activeSongId))
        if (searchParents(track.id, juce::String(track.id))) return;
}

void EngineSync::onSendCreated(const std::string& sendId) {
    for (auto& track : registry.tracksForSong(activeSongId)) {
        for (auto& send : registry.sendsForTrack(track.id)) {
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

    if (entityType == EntityType::Track) {
        for (auto& t : registry.tracksForSong(activeSongId)) {
            if (t.id == entityId) {
                engine.setTrackGain(id, t.outputGain);
                engine.setTrackMidiEnabled(id, t.midiEnabled);
                engine.renameTrack(id, juce::String(t.name));

                // Detect instrument change
                auto currentPlugin = engine.getTrackPluginName(id);
                if (!t.pluginId.empty()) {
                    auto plugin = registry.findPluginById(t.pluginId);
                    if (plugin && juce::String(plugin->name) != currentPlugin) {
                        if (currentPlugin.isNotEmpty())
                            engine.removeTrackInstrument(id);
                        engine.addTrackInstrument(id, juce::String(plugin->name),
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
    else if (entityType == EntityType::Bus) {
        for (auto& b : registry.bussesForSong(activeSongId)) {
            if (b.id == entityId) {
                engine.setBusGain(id, b.outputGain);
                engine.renameBus(id, juce::String(b.name));
                return;
            }
        }
    }
    else if (entityType == EntityType::Send) {
        for (auto& t : registry.tracksForSong(activeSongId)) {
            for (auto& s : registry.sendsForTrack(t.id)) {
                if (s.id == entityId) {
                    engine.setSendGain(juce::String(t.id), juce::String(s.busId), s.gain);
                    return;
                }
            }
        }
    }
    else if (entityType == EntityType::Song) {
        if (entityId == activeSongId)
            engine.setMasterGain(registry.getMasterGain(activeSongId));
    }
}

void EngineSync::onEntityDeleted(const std::string& entityType, const std::string& entityId) {
    auto id = juce::String(entityId);
    if (entityType == EntityType::Track)
        engine.removeTrack(id);
    else if (entityType == EntityType::Bus)
        engine.removeBus(id);
    else if (entityType == EntityType::Effect) {
        // Try all parents including master output
        engine.removeEffect(juce::String("Output"), id);
        for (auto& t : registry.tracksForSong(activeSongId))
            engine.removeEffect(juce::String(t.id), id);
        for (auto& b : registry.bussesForSong(activeSongId))
            engine.removeEffect(juce::String(b.id), id);
    }
}

// --- Event dispatch ---

void EngineSync::onRegistryEvent(const RegistryEvent& event) {
    // Detect song switch via config change
    if (event.entityType == "config" && event.entityId == "current_song_id") {
        auto newSongId = registry.getConfig("current_song_id");
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
