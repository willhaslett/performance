#include "engine/EngineSync.h"
#include "engine/AudioEngineInterface.h"
#include "engine/Log.h"
#include "api/StateAPI.h"
#include <juce_core/juce_core.h>

EngineSync::EngineSync(AudioEngineInterface& engine, StateAPI& stateAPI)
    : engine(engine), stateAPI(stateAPI) {
    subscriptionId = stateAPI.events().subscribe([this](const StateEvent& event) {
        onStateEvent(event);
    });
}

EngineSync::~EngineSync() {
    if (subscriptionId >= 0)
        stateAPI.events().unsubscribe(subscriptionId);
}

// --- State event handler ---

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
    // Song switch
    if (event.entity == StateEvent::Config && event.entityId == "current_song_id") {
        auto newSongId = stateAPI.getMasterOutputId();
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
            if (event.entity == StateEvent::Bus) onBusCreated(event.entityId);
            else if (event.entity == StateEvent::Track) onTrackCreated(event.entityId);
            else if (event.entity == StateEvent::Effect) onEffectCreated(event.entityId);
            else if (event.entity == StateEvent::Send) onSendCreated(event.entityId);
            break;
        case StateEvent::Updated:
            onEntityUpdated(type, event.entityId);
            break;
        case StateEvent::Deleted:
            onEntityDeleted(type, event.entityId);
            break;
    }
}

// --- Core engine operations ---

void EngineSync::clearEngine() {
    engine.clearAllTracks();
    engine.clearAllBusses();
}

void EngineSync::loadSong(const std::string& songId) {
    PERF_ASSERT(!songId.empty(), "loadSong called with empty songId");
    activeSongId = songId;
    clearEngine();

    auto* song = stateAPI.findSong(songId);
    PERF_ASSERT(song, "loadSong: song not found");

    perfLog("[EngineSync] loadSong: %s (%d tracks, %d busses)\n",
            songId.c_str(), (int)song->tracks.size(), (int)song->busses.size());

    // Rebuild: busses → tracks → effects → sends → master effects
    for (auto& bus : song->busses) onBusCreated(bus.id);
    for (auto& track : song->tracks) onTrackCreated(track.id);
    for (auto& bus : song->busses)
        for (auto& fx : bus.effects) onEffectCreated(fx.id);
    for (auto& track : song->tracks)
        for (auto& fx : track.effects) onEffectCreated(fx.id);
    for (auto& track : song->tracks)
        for (auto& send : track.sends) onSendCreated(send.id);
    for (auto& fx : song->masterEffects) onEffectCreated(fx.id);

    engine.setMasterGain(song->masterGain);
    engine.setMasterAudioEnabled(song->masterAudioEnabled);
}

// --- Entity handlers ---

void EngineSync::onBusCreated(const std::string& busId) {
    auto* bus = stateAPI.findBus(busId);
    PERF_ASSERT(bus, "onBusCreated: bus not found");
    engine.createBusWithId(juce::String(bus->id), juce::String(bus->name));
    engine.setBusGain(juce::String(bus->id), bus->outputGain);
}

void EngineSync::onTrackCreated(const std::string& trackId) {
    auto* track = stateAPI.findTrack(trackId);
    PERF_ASSERT(track, "onTrackCreated: track not found");

    // Action tracks have no audio engine representation
    if (track->sourceType == TrackSourceType::Action) return;

    if (track->sourceType == TrackSourceType::AudioInput) {
        engine.createAudioInputTrackWithId(juce::String(track->id), juce::String(track->name),
                                            track->inputChannelStart, track->inputChannelCount);
        engine.setTrackGain(juce::String(track->id), track->outputGain);
        if (!track->audioEnabled)
            engine.setTrackAudioEnabled(juce::String(track->id), false);
        return;
    }

    engine.createTrackWithId(juce::String(track->id), juce::String(track->name));
    engine.setTrackGain(juce::String(track->id), track->outputGain);
    if (!track->midiEnabled)
        engine.setTrackMidiEnabled(juce::String(track->id), false);
    if (!track->audioEnabled)
        engine.setTrackAudioEnabled(juce::String(track->id), false);

    if (!track->pluginId.empty()) {
        auto* plugin = stateAPI.findPluginById(track->pluginId);
        if (plugin) {
            stateAPI.setTrackInstrumentLoadStatus(track->id, LoadStatus::Pending);
            auto presetId = track->presetId;
            engine.addTrackInstrument(juce::String(track->id), juce::String(plugin->name),
                [this, id = track->id, presetId] {
                    perfLog("[EngineSync] Instrument loaded: %s\n", id.c_str());
                    stateAPI.setTrackInstrumentLoadStatus(id, LoadStatus::Loaded);
                    restorePresetState(id, "", presetId);
                });
        }
    }
}

void EngineSync::onEffectCreated(const std::string& effectId) {
    auto* fx = stateAPI.findEffect(effectId);
    if (!fx) {
        perfLog("[EngineSync] onEffectCreated: effect not found: %s\n", effectId.c_str());
        return;
    }

    auto* plugin = stateAPI.findPluginById(fx->pluginId);
    if (!plugin) {
        perfLog("[EngineSync] onEffectCreated: plugin '%s' not found (uninstalled?)\n", fx->pluginId.c_str());
        return;
    }

    // Determine engine parent ID (songId → "Output")
    auto* song = stateAPI.findSong(activeSongId);
    PERF_ASSERT(song, "onEffectCreated: active song not found");

    juce::String engineParentId;
    std::string stateParentId;  // for preset restore
    for (auto& mfx : song->masterEffects)
        if (mfx.id == effectId) { engineParentId = "Output"; stateParentId = song->id; break; }
    if (engineParentId.isEmpty())
        for (auto& t : song->tracks)
            for (auto& tfx : t.effects)
                if (tfx.id == effectId) { engineParentId = juce::String(t.id); stateParentId = t.id; break; }
    if (engineParentId.isEmpty())
        for (auto& b : song->busses)
            for (auto& bfx : b.effects)
                if (bfx.id == effectId) { engineParentId = juce::String(b.id); stateParentId = b.id; break; }

    if (engineParentId.isEmpty()) {
        perfLog("[EngineSync] onEffectCreated: no parent found for effect %s\n", effectId.c_str());
        return;
    }

    perfLog("[EngineSync] Loading effect %s on %s\n", plugin->name.c_str(), engineParentId.toRawUTF8());
    auto presetId = fx->presetId;
    stateAPI.setEffectLoadStatus(effectId, LoadStatus::Pending);
    engine.addEffect(engineParentId, juce::String(fx->id), juce::String(plugin->name),
        [this, stateParentId, effectId, presetId] {
            stateAPI.setEffectLoadStatus(effectId, LoadStatus::Loaded);
            restorePresetState(stateParentId, effectId, presetId);
        });
}

void EngineSync::restorePresetState(const std::string& parentId, const std::string& effectId,
                                     const std::string& presetId) {
    // Get the processor from the engine
    juce::AudioProcessor* proc = nullptr;
    auto engParentId = (parentId == activeSongId) ? juce::String("Output") : juce::String(parentId);
    if (effectId.empty())
        proc = engine.getTrackInstrumentProcessor(juce::String(parentId));
    else
        proc = engine.getEffectProcessor(engParentId, juce::String(effectId));
    if (!proc) return;

    // Priority 1: captured processor state (base64 blob from last save)
    std::string processorState;
    if (effectId.empty()) {
        auto* track = stateAPI.findTrack(parentId);
        if (track) processorState = track->processorState;
    } else {
        auto* fx = stateAPI.findEffect(effectId);
        if (fx) processorState = fx->processorState;
    }

    if (!processorState.empty()) {
        juce::MemoryBlock block;
        block.fromBase64Encoding(juce::String(processorState));
        if (block.getSize() > 0) {
            proc->setStateInformation(block.getData(), (int)block.getSize());
            perfLog("[EngineSync] Restored captured state (%d bytes)\n", (int)block.getSize());
            return;
        }
    }

    // Priority 2: named preset file
    if (presetId.empty()) return;
    auto* preset = stateAPI.findPresetById(presetId);
    if (!preset || preset->statePath.empty()) return;

    auto file = juce::File(juce::String(preset->statePath));
    if (!file.existsAsFile()) return;

    juce::MemoryBlock state;
    file.loadFileAsData(state);
    proc->setStateInformation(state.getData(), (int)state.getSize());
    perfLog("[EngineSync] Restored preset file: %s (%d bytes)\n",
            preset->name.c_str(), (int)state.getSize());
}

void EngineSync::onSendCreated(const std::string& sendId) {
    auto* song = stateAPI.findSong(activeSongId);
    PERF_ASSERT(song, "onSendCreated: active song not found");

    for (auto& track : song->tracks) {
        for (auto& send : track.sends) {
            if (send.id == sendId) {
                engine.addSend(juce::String(track.id), juce::String(send.busId), send.gain);
                return;
            }
        }
    }
}

void EngineSync::onEntityUpdated(const std::string& entityType, const std::string& entityId) {
    auto id = juce::String(entityId);

    if (entityType == "track") {
        auto* t = stateAPI.findTrack(entityId);
        PERF_ASSERT(t, "onEntityUpdated: track not found");
        if (t->sourceType == TrackSourceType::Action) return;  // no engine representation
        engine.setTrackGain(id, t->outputGain);
        engine.setTrackAudioEnabled(id, t->audioEnabled);
        engine.setTrackOutputTarget(id, juce::String(t->outputTarget));
        engine.renameTrack(id, juce::String(t->name));

        if (t->sourceType == TrackSourceType::AudioInput) {
            engine.setTrackInputChannels(id, t->inputChannelStart, t->inputChannelCount);
            return;
        }

        engine.setTrackMidiEnabled(id, t->midiEnabled);

        // Detect instrument change (skip if currently loading to avoid re-trigger loop)
        if (t->instrumentLoadStatus == LoadStatus::Pending) return;
        auto currentPlugin = engine.getTrackPluginName(id);
        if (!t->pluginId.empty()) {
            auto* plugin = stateAPI.findPluginById(t->pluginId);
            if (plugin && juce::String(plugin->name) != currentPlugin) {
                if (currentPlugin.isNotEmpty())
                    engine.removeTrackInstrument(id);
                stateAPI.setTrackInstrumentLoadStatus(t->id, LoadStatus::Pending);
                auto presetId = t->presetId;
                engine.addTrackInstrument(id, juce::String(plugin->name),
                    [this, trackId = t->id, presetId] {
                        perfLog("[EngineSync] Instrument loaded: %s\n", trackId.c_str());
                        stateAPI.setTrackInstrumentLoadStatus(trackId, LoadStatus::Loaded);
                        restorePresetState(trackId, "", presetId);
                    });
            }
        } else if (currentPlugin.isNotEmpty()) {
            engine.removeTrackInstrument(id);
        }
    }
    else if (entityType == "bus") {
        auto* b = stateAPI.findBus(entityId);
        PERF_ASSERT(b, "onEntityUpdated: bus not found");
        engine.setBusGain(id, b->outputGain);
        engine.setBusAudioEnabled(id, b->audioEnabled);
        engine.setBusOutputTarget(id, juce::String(b->outputTarget));
        engine.renameBus(id, juce::String(b->name));
    }
    else if (entityType == "send") {
        auto* song = stateAPI.findSong(activeSongId);
        PERF_ASSERT(song, "onEntityUpdated(send): active song not found");
        for (auto& t : song->tracks)
            for (auto& s : t.sends)
                if (s.id == entityId) {
                    engine.setSendGain(juce::String(t.id), juce::String(s.busId), s.gain);
                    return;
                }
    }
    else if (entityType == "song") {
        if (entityId == activeSongId) {
            auto* song = stateAPI.findSong(activeSongId);
            PERF_ASSERT(song, "onEntityUpdated(song): active song not found");
            engine.setMasterGain(song->masterGain);
            engine.setMasterAudioEnabled(song->masterAudioEnabled);
        }
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
        auto* song = stateAPI.findSong(activeSongId);
        PERF_ASSERT(song, "onEntityDeleted(effect): active song not found");
        engine.removeEffect(juce::String("Output"), id);
        for (auto& t : song->tracks)
            engine.removeEffect(juce::String(t.id), id);
        for (auto& b : song->busses)
            engine.removeEffect(juce::String(b.id), id);
    }
}
