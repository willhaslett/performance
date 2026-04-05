#include "engine/EngineSync.h"
#include "engine/AudioEngine.h"
#include "engine/Log.h"
#include "registry/Registry.h"

EngineSync::EngineSync(AudioEngine& engine, Registry& registry)
    : engine(engine), registry(registry) {}

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

            // Bus effects
            for (auto& fx : registry.effectsForParent(bus.id)) {
                if (engineEffectIds.count(fx.id)) continue;
                auto plugin = registry.findPluginById(fx.pluginId);
                if (plugin) {
                    engine.addBusEffect(juce::String(bus.name),
                                         juce::String(fx.name),
                                         juce::String(plugin->name));
                    engineEffectIds.insert(fx.id);
                }
            }
        }
    }

    // 2. Sync tracks
    auto regTracks = registry.tracksForSong(songId);
    for (auto& track : regTracks) {
        if (engineTrackNames.find(track.name) == engineTrackNames.end()) {
            auto plugin = registry.findPluginById(track.pluginId);
            if (!plugin) {
                perfLog("[EngineSync] Plugin not found for track \"%s\", skipping\n",
                        track.name.c_str());
                continue;
            }

            engine.createTrack(juce::String(track.name));
            engineTrackNames.insert(track.name);

            // Resolve snapshot
            juce::String snapshotName;
            if (!track.snapshotId.empty()) {
                auto snap = registry.findSnapshotById(track.snapshotId);
                if (snap) snapshotName = juce::String(snap->name);
            }

            engine.addTrackInstrument(juce::String(track.name), juce::String(plugin->name),
                [snapshotName, trackName = track.name] {
                    perfLog("[EngineSync] Instrument loaded: %s\n", trackName.c_str());
                });

            // Track effects
            for (auto& fx : registry.effectsForParent(track.id)) {
                if (engineEffectIds.count(fx.id)) continue;
                auto fxPlugin = registry.findPluginById(fx.pluginId);
                if (fxPlugin) {
                    engine.addTrackEffect(juce::String(track.name),
                                           juce::String(fx.name),
                                           juce::String(fxPlugin->name));
                    engineEffectIds.insert(fx.id);
                }
            }

            // Gain and MIDI
            engine.setTrackGain(juce::String(track.name), track.outputGain);
            if (!track.midiEnabled)
                engine.setTrackMidiEnabled(juce::String(track.name), false);
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
