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

void EngineSync::onRegistryEvent(const RegistryEvent& event) {
    if (event.action != RegistryEvent::Updated) return;
    if (activeSongId.empty()) return;

    auto id = juce::String(event.entityId);

    if (event.entityType == EntityType::Track) {
        // Read updated track from registry, apply continuous values to engine
        for (auto& t : registry.tracksForSong(activeSongId)) {
            if (t.id == event.entityId) {
                engine.setTrackGain(id, t.outputGain);
                engine.setTrackMidiEnabled(id, t.midiEnabled);
                return;
            }
        }
    }
    else if (event.entityType == EntityType::Bus) {
        for (auto& b : registry.bussesForSong(activeSongId)) {
            if (b.id == event.entityId) {
                engine.setBusGain(id, b.outputGain);
                return;
            }
        }
    }
    else if (event.entityType == EntityType::Send) {
        // Find the send and update its gain in the engine
        for (auto& t : registry.tracksForSong(activeSongId)) {
            for (auto& s : registry.sendsForTrack(t.id)) {
                if (s.id == event.entityId) {
                    engine.setSendGain(juce::String(t.id), juce::String(s.busId), s.gain);
                    return;
                }
            }
        }
    }
    else if (event.entityType == EntityType::Song) {
        if (event.entityId == activeSongId) {
            engine.setMasterGain(registry.getMasterGain(activeSongId));
        }
    }
}

void EngineSync::notifyRemoved(const std::string& id) {
    engineTrackIds.erase(id);
    engineBusIds.erase(id);
    // Effect and send IDs are orphaned by CASCADE delete —
    // clear them so they don't block re-creation
    engineEffectIds.clear();
    engineSendIds.clear();
}

void EngineSync::clear() {
    engine.clearAllTracks();
    engine.clearAllBusses();
    engineTrackIds.clear();
    engineBusIds.clear();
    engineEffectIds.clear();
    engineSendIds.clear();
}

void EngineSync::sync(const std::string& songId) {
    if (songId.empty()) return;

    // 1. Sync busses (must exist before sends)
    auto regBusses = registry.bussesForSong(songId);
    for (auto& bus : regBusses) {
        if (engineBusIds.find(bus.id) == engineBusIds.end()) {
            engine.createBusWithId(juce::String(bus.id), juce::String(bus.name));
            engine.setBusGain(juce::String(bus.id), bus.outputGain);
            engineBusIds.insert(bus.id);
        }

        // Effects — always sync
        for (auto& fx : registry.effectsForParent(bus.id)) {
            if (engineEffectIds.count(fx.id)) continue;
            auto plugin = registry.findPluginById(fx.pluginId);
            if (plugin) {
                engine.addEffect(juce::String(bus.id),
                                  juce::String(fx.name),
                                  juce::String(plugin->name));
                engineEffectIds.insert(fx.id);
            }
        }
    }

    // 2. Sync tracks
    auto regTracks = registry.tracksForSong(songId);
    for (auto& track : regTracks) {
        if (engineTrackIds.find(track.id) == engineTrackIds.end()) {
            engine.createTrackWithId(juce::String(track.id), juce::String(track.name));
            engineTrackIds.insert(track.id);

            // Load instrument if plugin is set
            if (!track.pluginId.empty()) {
                auto plugin = registry.findPluginById(track.pluginId);
                if (plugin) {
                    juce::String presetName;
                    if (!track.presetId.empty()) {
                        auto snap = registry.findPresetById(track.presetId);
                        if (snap) presetName = juce::String(snap->name);
                    }

                    engine.addTrackInstrument(juce::String(track.id), juce::String(plugin->name),
                        [presetName, trackId = track.id] {
                            perfLog("[EngineSync] Instrument loaded: %s\n", trackId.c_str());
                        });
                }
            }

            // Gain and MIDI
            engine.setTrackGain(juce::String(track.id), track.outputGain);
            if (!track.midiEnabled)
                engine.setTrackMidiEnabled(juce::String(track.id), false);
        }

        // Effects — always sync, even for existing tracks
        for (auto& fx : registry.effectsForParent(track.id)) {
            if (engineEffectIds.count(fx.id)) continue;
            auto fxPlugin = registry.findPluginById(fx.pluginId);
            if (fxPlugin) {
                engine.addEffect(juce::String(track.id),
                                  juce::String(fx.name),
                                  juce::String(fxPlugin->name));
                engineEffectIds.insert(fx.id);
            }
        }
    }

    // 3. Sync sends (tracks and busses must exist)
    for (auto& track : regTracks) {
        if (engineTrackIds.find(track.id) == engineTrackIds.end()) continue;

        for (auto& send : registry.sendsForTrack(track.id)) {
            if (engineSendIds.count(send.id)) continue;
            // send.busId is the registry bus UUID — same as engine bus UUID
            engine.addSend(juce::String(track.id),
                            juce::String(send.busId), send.gain);
            engineSendIds.insert(send.id);
        }
    }

    // 4. Master gain
    engine.setMasterGain(registry.getMasterGain(songId));
}

// persistState removed — continuous values are now written to registry
// at the point of mutation in PerformanceAPI, not on a timer.
