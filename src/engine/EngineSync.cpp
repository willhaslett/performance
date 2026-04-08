#include "engine/EngineSync.h"
#include "engine/AudioEngine.h"
#include "engine/Log.h"
#include "api/StateAPI.h"

EngineSync::EngineSync(AudioEngine& engine, StateAPI& stateAPI)
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
    if (songId.empty()) return;
    activeSongId = songId;
    clearEngine();

    auto* song = stateAPI.findSong(songId);
    if (!song) return;

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
}

// --- Entity handlers ---

void EngineSync::onBusCreated(const std::string& busId) {
    auto* bus = stateAPI.findBus(busId);
    if (!bus) return;
    engine.createBusWithId(juce::String(bus->id), juce::String(bus->name));
    engine.setBusGain(juce::String(bus->id), bus->outputGain);
}

void EngineSync::onTrackCreated(const std::string& trackId) {
    auto* track = stateAPI.findTrack(trackId);
    if (!track) return;

    engine.createTrackWithId(juce::String(track->id), juce::String(track->name));
    engine.setTrackGain(juce::String(track->id), track->outputGain);
    if (!track->midiEnabled)
        engine.setTrackMidiEnabled(juce::String(track->id), false);

    if (!track->pluginId.empty()) {
        auto* plugin = stateAPI.findPluginById(track->pluginId);
        if (plugin) {
            engine.addTrackInstrument(juce::String(track->id), juce::String(plugin->name),
                [id = track->id] {
                    perfLog("[EngineSync] Instrument loaded: %s\n", id.c_str());
                });
        }
    }
}

void EngineSync::onEffectCreated(const std::string& effectId) {
    auto* fx = stateAPI.findEffect(effectId);
    if (!fx) return;

    auto* plugin = stateAPI.findPluginById(fx->pluginId);
    if (!plugin) return;

    // Determine engine parent ID (songId → "Output")
    auto* song = stateAPI.findSong(activeSongId);
    if (!song) return;

    juce::String engineParentId;
    for (auto& mfx : song->masterEffects)
        if (mfx.id == effectId) { engineParentId = "Output"; break; }
    if (engineParentId.isEmpty())
        for (auto& t : song->tracks)
            for (auto& tfx : t.effects)
                if (tfx.id == effectId) { engineParentId = juce::String(t.id); break; }
    if (engineParentId.isEmpty())
        for (auto& b : song->busses)
            for (auto& bfx : b.effects)
                if (bfx.id == effectId) { engineParentId = juce::String(b.id); break; }

    if (engineParentId.isEmpty()) return;

    engine.addEffect(engineParentId, juce::String(fx->id), juce::String(plugin->name));
}

void EngineSync::onSendCreated(const std::string& sendId) {
    auto* song = stateAPI.findSong(activeSongId);
    if (!song) return;

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
        if (!t) return;
        engine.setTrackGain(id, t->outputGain);
        engine.setTrackMidiEnabled(id, t->midiEnabled);
        engine.renameTrack(id, juce::String(t->name));

        // Detect instrument change
        auto currentPlugin = engine.getTrackPluginName(id);
        if (!t->pluginId.empty()) {
            auto* plugin = stateAPI.findPluginById(t->pluginId);
            if (plugin && juce::String(plugin->name) != currentPlugin) {
                if (currentPlugin.isNotEmpty())
                    engine.removeTrackInstrument(id);
                engine.addTrackInstrument(id, juce::String(plugin->name),
                    [trackId = t->id] {
                        perfLog("[EngineSync] Instrument loaded: %s\n", trackId.c_str());
                    });
            }
        } else if (currentPlugin.isNotEmpty()) {
            engine.removeTrackInstrument(id);
        }
    }
    else if (entityType == "bus") {
        auto* b = stateAPI.findBus(entityId);
        if (!b) return;
        engine.setBusGain(id, b->outputGain);
        engine.renameBus(id, juce::String(b->name));
    }
    else if (entityType == "send") {
        auto* song = stateAPI.findSong(activeSongId);
        if (!song) return;
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
            if (song) engine.setMasterGain(song->masterGain);
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
        engine.removeEffect(juce::String("Output"), id);
        auto* song = stateAPI.findSong(activeSongId);
        if (song) {
            for (auto& t : song->tracks)
                engine.removeEffect(juce::String(t.id), id);
            for (auto& b : song->busses)
                engine.removeEffect(juce::String(b.id), id);
        }
    }
}
