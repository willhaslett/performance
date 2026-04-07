#include "engine/EngineSync.h"
#include "engine/AudioEngine.h"
#include "engine/Log.h"
#include "registry/Registry.h"

EngineSync::EngineSync(AudioEngine& engine, Registry& registry)
    : engine(engine), registry(registry) {}

void EngineSync::notifyRemoved(const std::string& name) {
    engineTrackNames.erase(name);
    engineBusNames.erase(name);
    // Effect and send IDs are orphaned by CASCADE delete —
    // clear them so they don't block re-creation
    engineEffectIds.clear();
    engineSendIds.clear();
}

void EngineSync::notifyRenamed(const std::string& oldName, const std::string& newName) {
    if (engineTrackNames.erase(oldName))
        engineTrackNames.insert(newName);
    if (engineBusNames.erase(oldName))
        engineBusNames.insert(newName);
}

void EngineSync::clear() {
    engine.clearAllTracks();
    engine.clearAllBusses();
    engineTrackNames.clear();
    engineBusNames.clear();
    engineEffectIds.clear();
    engineSendIds.clear();
}

void EngineSync::sync(const std::string& songId) {
    if (songId.empty()) return;

    // 1. Sync busses (must exist before sends)
    auto regBusses = registry.bussesForSong(songId);
    for (auto& bus : regBusses) {
        if (engineBusNames.find(bus.name) == engineBusNames.end()) {
            engine.createBus(juce::String(bus.name));
            engine.setBusGain(juce::String(bus.name), bus.outputGain);
            engineBusNames.insert(bus.name);
        }

        // Effects — always sync
        for (auto& fx : registry.effectsForParent(bus.id)) {
            if (engineEffectIds.count(fx.id)) continue;
            auto plugin = registry.findPluginById(fx.pluginId);
            if (plugin) {
                engine.addEffect(juce::String(bus.name),
                                  juce::String(fx.name),
                                  juce::String(plugin->name));
                engineEffectIds.insert(fx.id);
            }
        }
    }

    // 2. Sync tracks
    auto regTracks = registry.tracksForSong(songId);
    for (auto& track : regTracks) {
        if (engineTrackNames.find(track.name) == engineTrackNames.end()) {
            engine.createTrack(juce::String(track.name));
            engineTrackNames.insert(track.name);

            // Load instrument if plugin is set
            if (!track.pluginId.empty()) {
                auto plugin = registry.findPluginById(track.pluginId);
                if (plugin) {
                    juce::String presetName;
                    if (!track.presetId.empty()) {
                        auto snap = registry.findPresetById(track.presetId);
                        if (snap) presetName = juce::String(snap->name);
                    }

                    engine.addTrackInstrument(juce::String(track.name), juce::String(plugin->name),
                        [presetName, trackName = track.name] {
                            perfLog("[EngineSync] Instrument loaded: %s\n", trackName.c_str());
                        });
                }
            }

            // Gain and MIDI
            engine.setTrackGain(juce::String(track.name), track.outputGain);
            if (!track.midiEnabled)
                engine.setTrackMidiEnabled(juce::String(track.name), false);
        }

        // Effects — always sync, even for existing tracks
        for (auto& fx : registry.effectsForParent(track.id)) {
            if (engineEffectIds.count(fx.id)) continue;
            auto fxPlugin = registry.findPluginById(fx.pluginId);
            if (fxPlugin) {
                engine.addEffect(juce::String(track.name),
                                  juce::String(fx.name),
                                  juce::String(fxPlugin->name));
                engineEffectIds.insert(fx.id);
            }
        }
    }

    // 3. Sync sends (tracks and busses must exist)
    for (auto& track : regTracks) {
        if (engineTrackNames.find(track.name) == engineTrackNames.end()) continue;

        for (auto& send : registry.sendsForTrack(track.id)) {
            if (engineSendIds.count(send.id)) continue;
            for (auto& bus : regBusses) {
                if (bus.id == send.busId) {
                    engine.addSend(juce::String(track.name),
                                    juce::String(bus.name), send.gain);
                    engineSendIds.insert(send.id);
                    break;
                }
            }
        }
    }
}

// persistState removed — continuous values are now written to registry
// at the point of mutation in PerformanceAPI, not on a timer.
