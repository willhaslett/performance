#include "api/StateAPI.h"
#include "engine/Log.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>

// Gain range matches the mixer fader: [-60dB, +6dB] — -60dB (0.001 linear)
// and below snap to true 0.0 so the fader at its floor produces actual
// silence, not barely-audible. +6dB matches the trackVolume binding's max
// (cubic * 2.0f). Every gain setter routes through this so no caller can
// blast past the fader's visible range.
static float clampGain(float g) {
    constexpr float kMinAudibleGain = 0.001f;  // -60dB = fader floor
    constexpr float kMaxGain        = 2.0f;    // +6dB  = fader top
    if (!std::isfinite(g) || g <= kMinAudibleGain) return 0.0f;
    return std::min(g, kMaxGain);
}

StateAPI::StateAPI() {}

// --- UUID generation (no JUCE dependency) ---

std::string StateAPI::generateId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << dist(gen);
    ss << std::setw(8) << dist(gen);
    ss << std::setw(8) << dist(gen);
    ss << std::setw(8) << dist(gen);
    return ss.str();
}

// --- Asserting accessors (crash on programming error) ---

SongState& StateAPI::song() {
    PERF_ASSERT(!state.currentSongId.empty(), "No current song set");
    auto* s = findSong(state.currentSongId);
    PERF_ASSERT(s, "currentSongId references nonexistent song");
    return *s;
}

const SongState& StateAPI::song() const {
    PERF_ASSERT(!state.currentSongId.empty(), "No current song set");
    auto* s = findSong(state.currentSongId);
    PERF_ASSERT(s, "currentSongId references nonexistent song");
    return *s;
}

TrackState& StateAPI::track(const TrackId& id) {
    auto& s = song();
    for (auto& t : s.tracks)
        if (t.id == id) return t;
    PERF_ASSERT(false, "Track not found");
    __builtin_unreachable();
}

const TrackState& StateAPI::track(const TrackId& id) const {
    auto& s = song();
    for (auto& t : s.tracks)
        if (t.id == id) return t;
    PERF_ASSERT(false, "Track not found");
    __builtin_unreachable();
}

BusState& StateAPI::bus(const BusId& id) {
    auto& s = song();
    for (auto& b : s.busses)
        if (b.id == id) return b;
    PERF_ASSERT(false, "Bus not found");
    __builtin_unreachable();
}

const BusState& StateAPI::bus(const BusId& id) const {
    auto& s = song();
    for (auto& b : s.busses)
        if (b.id == id) return b;
    PERF_ASSERT(false, "Bus not found");
    __builtin_unreachable();
}

DeviceState& StateAPI::device(const DeviceId& id) {
    for (auto& d : state.devices)
        if (d.id == id) return d;
    PERF_ASSERT(false, "Device not found");
    __builtin_unreachable();
}

// --- Public nullable helpers (unchanged) ---

SongState* StateAPI::currentSong() {
    if (state.currentSongId.empty()) return nullptr;
    return findSong(state.currentSongId);
}

const SongState* StateAPI::currentSong() const {
    if (state.currentSongId.empty()) return nullptr;
    return findSong(state.currentSongId);
}

SongState* StateAPI::findSong(const SongId& id) {
    for (auto& s : state.songs)
        if (s.id == id) return &s;
    return nullptr;
}

const SongState* StateAPI::findSong(const SongId& id) const {
    for (auto& s : state.songs)
        if (s.id == id) return &s;
    return nullptr;
}

TrackState* StateAPI::findTrack(const TrackId& id) {
    auto* s = currentSong();
    if (!s) return nullptr;
    for (auto& t : s->tracks)
        if (t.id == id) return &t;
    return nullptr;
}

const TrackState* StateAPI::findTrack(const TrackId& id) const {
    auto* s = currentSong();
    if (!s) return nullptr;
    for (auto& t : s->tracks)
        if (t.id == id) return &t;
    return nullptr;
}

BusState* StateAPI::findBus(const BusId& id) {
    auto* s = currentSong();
    if (!s) return nullptr;
    for (auto& b : s->busses)
        if (b.id == id) return &b;
    return nullptr;
}

const BusState* StateAPI::findBus(const BusId& id) const {
    auto* s = currentSong();
    if (!s) return nullptr;
    for (auto& b : s->busses)
        if (b.id == id) return &b;
    return nullptr;
}

std::vector<EffectState>* StateAPI::findEffectList(const EffectId& effectId, std::string* outParentId) {
    auto& s = song();
    for (auto& fx : s.masterEffects) {
        if (fx.id == effectId) {
            if (outParentId) *outParentId = s.id.str();
            return &s.masterEffects;
        }
    }
    for (auto& t : s.tracks) {
        for (auto& fx : t.effects) {
            if (fx.id == effectId) {
                if (outParentId) *outParentId = t.id.str();
                return &t.effects;
            }
        }
    }
    for (auto& b : s.busses) {
        for (auto& fx : b.effects) {
            if (fx.id == effectId) {
                if (outParentId) *outParentId = b.id.str();
                return &b.effects;
            }
        }
    }
    return nullptr;
}

std::vector<SendState>* StateAPI::findSendList(const SendId& sendId, std::string* outTrackId) {
    auto& s = song();
    for (auto& t : s.tracks) {
        for (auto& send : t.sends) {
            if (send.id == sendId) {
                if (outTrackId) *outTrackId = t.id.str();
                return &t.sends;
            }
        }
    }
    return nullptr;
}

const EffectState* StateAPI::findEffect(const EffectId& effectId) const {
    auto& s = song();
    for (auto& fx : s.masterEffects)
        if (fx.id == effectId) return &fx;
    for (auto& t : s.tracks)
        for (auto& fx : t.effects)
            if (fx.id == effectId) return &fx;
    for (auto& b : s.busses)
        for (auto& fx : b.effects)
            if (fx.id == effectId) return &fx;
    return nullptr;
}

void StateAPI::setEffectPresetId(const EffectId& effectId, const PresetId& presetId) {
    auto* list = findEffectList(effectId, nullptr);
    if (!list) { perfLog("[StateAPI] setEffectPresetId: effect '%s' not found\n", effectId.c_str()); return; }
    for (auto& fx : *list) {
        if (fx.id == effectId) {
            fx.presetId = presetId;
            markDirty();
            return;
        }
    }
}

// --- Song ---

SongId StateAPI::createSong(const std::string& name) {
    SongState s;
    s.id = SongId{generateId()};
    s.name = name;
    state.songs.push_back(std::move(s));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Song, state.songs.back().id.str(), "" });
    return state.songs.back().id;
}

void StateAPI::deleteSong(const SongId& id) {
    auto it = std::find_if(state.songs.begin(), state.songs.end(),
                           [&](auto& s) { return s.id == id; });
    if (it == state.songs.end()) return;  // idempotent
    SongId songId = it->id;
    state.songs.erase(it);
    if (state.currentSongId == songId)
        state.currentSongId = SongId{};
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Song, songId.str(), "" });
}

const std::vector<SongState>& StateAPI::allSongs() const {
    return state.songs;
}

void StateAPI::setCurrentSong(const SongId& songId) {
    if (state.currentSongId == songId) return;
    state.currentSongId = songId;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Config, "current_song_id", "" });
}

void StateAPI::setMasterGain(float gain) {
    pushUndo();
    auto& s = song();
    s.masterGain = gain;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Song, s.id.str(), "" });
}

float StateAPI::getMasterGain() const {
    auto* s = currentSong();
    return s ? s->masterGain : 1.0f;
}

void StateAPI::setSongTempo(double bpm) {
    pushUndo();
    auto& s = song();
    if (s.tempoEvents.empty())
        s.tempoEvents.push_back({ 0.0, bpm });
    else
        s.tempoEvents[0].bpm = bpm;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Song, s.id.str(), "" });
}

double StateAPI::getSongTempo() const {
    auto* s = currentSong();
    if (s && !s->tempoEvents.empty()) return s->tempoEvents[0].bpm;
    return 120.0;
}

void StateAPI::setSongTimeSignature(int numerator, int denominator) {
    pushUndo();
    auto& s = song();
    if (s.timeSigEvents.empty())
        s.timeSigEvents.push_back({ 0.0, numerator, denominator });
    else {
        s.timeSigEvents[0].numerator = numerator;
        s.timeSigEvents[0].denominator = denominator;
    }
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Song, s.id.str(), "" });
}

std::pair<int,int> StateAPI::getSongTimeSignature() const {
    auto* s = currentSong();
    if (s && !s->timeSigEvents.empty())
        return { s->timeSigEvents[0].numerator, s->timeSigEvents[0].denominator };
    return { 4, 4 };
}

std::string StateAPI::getMasterOutputId() const {
    return state.currentSongId.str();
}

// --- Tracks ---

TrackId StateAPI::createTrack(const std::string& name) {
    pushUndo();
    auto& s = song();
    TrackState t;
    t.id = TrackId{generateId()};
    t.name = name;
    t.position = (int)s.tracks.size();
    s.tracks.push_back(std::move(t));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Track, s.tracks.back().id.str(), "" });
    return s.tracks.back().id;
}

TrackId StateAPI::createAudioInputTrack(const std::string& name, int inputChannelStart,
                                             int inputChannelCount) {
    pushUndo();
    auto& s = song();
    TrackState t;
    t.id = TrackId{generateId()};
    t.name = name;
    t.position = (int)s.tracks.size();
    t.sourceType = TrackSourceType::AudioInput;
    t.channelMode = (inputChannelCount == 1) ? ChannelMode::Mono : ChannelMode::Stereo;
    t.inputChannelStart = inputChannelStart;
    t.inputChannelCount = inputChannelCount;
    s.tracks.push_back(std::move(t));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Track, s.tracks.back().id.str(), "" });
    return s.tracks.back().id;
}

TrackId StateAPI::createActionTrack(const std::string& name) {
    auto& s = song();
    TrackState t;
    t.id = TrackId{generateId()};
    t.name = name;
    t.position = (int)s.tracks.size();
    t.sourceType = TrackSourceType::Action;
    s.tracks.push_back(std::move(t));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Track, s.tracks.back().id.str(), "" });
    return s.tracks.back().id;
}

void StateAPI::removeTrack(const TrackId& id) {
    pushUndo();
    auto& s = song();
    auto it = std::find_if(s.tracks.begin(), s.tracks.end(),
                           [&](auto& t) { return t.id == id; });
    if (it == s.tracks.end()) return;  // idempotent
    s.tracks.erase(it);
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Track, id.str(), "" });
}

void StateAPI::renameTrack(const TrackId& id, const std::string& name) {
    pushUndo();
    auto& t = track(id);
    t.name = name;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

void StateAPI::moveTrack(const TrackId& id, int newPosition) {
    pushUndo();
    auto& s = song();
    auto& t = track(id);

    int oldPos = t.position;
    if (oldPos == newPosition) return;

    if (newPosition < oldPos) {
        for (auto& tr : s.tracks)
            if (tr.position >= newPosition && tr.position < oldPos)
                tr.position++;
    } else {
        for (auto& tr : s.tracks)
            if (tr.position > oldPos && tr.position <= newPosition)
                tr.position--;
    }
    t.position = newPosition;

    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

void StateAPI::setTrackGain(const TrackId& id, float gain) {
    pushUndo();
    track(id).outputGain = clampGain(gain);
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

float StateAPI::getTrackGain(const TrackId& id) const {
    return track(id).outputGain;
}


void StateAPI::setTrackOutputTarget(const TrackId& id, const std::string& target) {
    pushUndo();
    track(id).outputTarget = target;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

std::string StateAPI::getTrackOutputTarget(const TrackId& id) const {
    return track(id).outputTarget;
}

void StateAPI::setTrackArmed(const TrackId& id, bool armed) {
    track(id).armed = armed;
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

bool StateAPI::isTrackArmed(const TrackId& id) const {
    return track(id).armed;
}

void StateAPI::setTrackInputMonitoring(const TrackId& id, bool enabled) {
    pushUndo();
    track(id).inputMonitoring = enabled;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

bool StateAPI::isTrackInputMonitoring(const TrackId& id) const {
    return track(id).inputMonitoring;
}

void StateAPI::setTrackMuted(const TrackId& id, bool muted) {
    track(id).muted = muted;
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

bool StateAPI::isTrackMuted(const TrackId& id) const {
    return track(id).muted;
}

void StateAPI::setTrackSoloed(const TrackId& id, bool soloed) {
    track(id).soloed = soloed;
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

bool StateAPI::isTrackSoloed(const TrackId& id) const {
    return track(id).soloed;
}

bool StateAPI::isAnySoloed() const {
    auto* s = currentSong();
    if (!s) return false;
    for (auto& t : s->tracks)
        if (t.soloed) return true;
    return false;
}

void StateAPI::setTrackPlugin(const TrackId& id, const PluginId& pluginId,
                               const PresetId& presetId) {
    pushUndo();
    auto& t = track(id);
    t.pluginId = pluginId;
    t.presetId = presetId;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

void StateAPI::clearTrackPlugin(const TrackId& id) {
    pushUndo();
    setTrackPlugin(id, PluginId{}, PresetId{});
}

void StateAPI::setTrackPresetId(const TrackId& id, const PresetId& presetId) {
    track(id).presetId = presetId;
    markDirty();
}

void StateAPI::setTrackInputChannels(const TrackId& id, int start, int count) {
    pushUndo();
    auto& t = track(id);
    t.inputChannelStart = start;
    t.inputChannelCount = count;
    t.channelMode = (count == 1) ? ChannelMode::Mono : ChannelMode::Stereo;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

void StateAPI::setTrackInstrumentLoadStatus(const TrackId& id, LoadStatus status) {
    track(id).instrumentLoadStatus = status;
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id.str(), "" });
}

void StateAPI::setEffectLoadStatus(const EffectId& effectId, LoadStatus status) {
    std::string parentId;
    auto* list = findEffectList(effectId, &parentId);
    if (!list) { perfLog("[StateAPI] setEffectLoadStatus: effect '%s' not found\n", effectId.c_str()); return; }
    for (auto& fx : *list) {
        if (fx.id == effectId) {
            fx.loadStatus = status;
            eventBus.emit({ StateEvent::Updated, StateEvent::Effect, effectId.str(), parentId });
            return;
        }
    }
}

// --- Busses ---

BusId StateAPI::createBus(const std::string& name) {
    pushUndo();
    auto& s = song();
    BusState b;
    b.id = BusId{generateId()};
    b.name = name;
    b.position = (int)s.busses.size();
    s.busses.push_back(std::move(b));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Bus, s.busses.back().id.str(), "" });
    return s.busses.back().id;
}

void StateAPI::removeBus(const BusId& id) {
    pushUndo();
    auto& s = song();
    auto it = std::find_if(s.busses.begin(), s.busses.end(),
                           [&](auto& b) { return b.id == id; });
    if (it == s.busses.end()) return;  // idempotent
    s.busses.erase(it);
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Bus, id.str(), "" });
}

void StateAPI::renameBus(const BusId& id, const std::string& name) {
    pushUndo();
    bus(id).name = name;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Bus, id.str(), "" });
}

void StateAPI::setBusGain(const BusId& id, float gain) {
    pushUndo();
    bus(id).outputGain = clampGain(gain);
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Bus, id.str(), "" });
}

float StateAPI::getBusGain(const BusId& id) const {
    return bus(id).outputGain;
}

void StateAPI::setBusOutputTarget(const BusId& id, const std::string& target) {
    pushUndo();
    bus(id).outputTarget = target;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Bus, id.str(), "" });
}

std::string StateAPI::getBusOutputTarget(const BusId& id) const {
    return bus(id).outputTarget;
}

// --- Effects ---

EffectId StateAPI::addEffect(const std::string& parentId, const std::string& name,
                              const PluginId& pluginId) {
    pushUndo();
    auto& s = song();

    EffectState effect;
    effect.id = EffectId{generateId()};
    effect.name = name;
    effect.pluginId = pluginId;

    std::vector<EffectState>* list = nullptr;

    if (parentId == s.id.str()) {
        list = &s.masterEffects;
    } else {
        for (auto& t : s.tracks)
            if (t.id.str() == parentId) { list = &t.effects; break; }
        if (!list) {
            for (auto& b : s.busses)
                if (b.id.str() == parentId) { list = &b.effects; break; }
        }
    }

    if (!list) {
        perfLog("[StateAPI] addEffect: parentId '%s' not found\n", parentId.c_str());
        return {};
    }
    effect.position = (int)list->size();
    list->push_back(std::move(effect));
    auto& added = list->back();
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Effect, added.id.str(), parentId });
    return added.id;
}

void StateAPI::removeEffect(const EffectId& effectId) {
    pushUndo();
    std::string parentId;
    auto* list = findEffectList(effectId, &parentId);
    if (!list) { perfLog("[StateAPI] removeEffect: effect '%s' not found\n", effectId.c_str()); return; }
    list->erase(std::remove_if(list->begin(), list->end(),
                               [&](auto& fx) { return fx.id == effectId; }),
                list->end());
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Effect, effectId.str(), parentId });
}

// --- Sends ---

SendId StateAPI::addSend(const TrackId& trackId, const BusId& busId, float gain) {
    pushUndo();
    auto& t = track(trackId);
    SendState send;
    send.id = SendId{generateId()};
    send.busId = busId;
    send.gain = clampGain(gain);
    t.sends.push_back(std::move(send));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Send, t.sends.back().id.str(), trackId.str() });
    return t.sends.back().id;
}

void StateAPI::removeSend(const SendId& sendId) {
    pushUndo();
    std::string trackId;
    auto* list = findSendList(sendId, &trackId);
    if (!list) { perfLog("[StateAPI] removeSend: send '%s' not found\n", sendId.c_str()); return; }
    list->erase(std::remove_if(list->begin(), list->end(),
                               [&](auto& s) { return s.id == sendId; }),
                list->end());
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Send, sendId.str(), trackId });
}

void StateAPI::setSendGainByBus(const TrackId& trackId, const BusId& busId, float gain) {
    pushUndo();
    auto& t = track(trackId);
    for (auto& s : t.sends) {
        if (s.busId == busId) {
            setSendGain(s.id, gain);
            return;
        }
    }
}

void StateAPI::setSendGain(const SendId& sendId, float gain) {
    pushUndo();
    auto& s = song();
    for (auto& t : s.tracks) {
        for (auto& send : t.sends) {
            if (send.id == sendId) {
                send.gain = clampGain(gain);
                markDirty();
                eventBus.emit({ StateEvent::Updated, StateEvent::Send, sendId.str(), t.id.str() });
                return;
            }
        }
    }
}

// --- Bindings ---

BindingId StateAPI::addBinding(const SongId& songId, const std::string& controlType,
                                int channel, int number, const ActionId& actionId,
                                const std::string& args, const std::string& description,
                                const DeviceId& deviceId) {
    pushUndo();
    auto* s = findSong(songId);
    PERF_ASSERT(s, "addBinding: song not found");
    BindingState binding;
    binding.id = BindingId{generateId()};
    binding.songId = songId;
    binding.deviceId = deviceId;
    binding.controlType = controlType;
    binding.channel = channel;
    binding.number = number;
    binding.actionId = actionId;
    binding.args = args;
    binding.description = description;
    s->bindings.push_back(std::move(binding));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Binding, s->bindings.back().id.str(), songId.str() });
    return s->bindings.back().id;
}

BindingId StateAPI::addGlobalBinding(const std::string& controlType, int channel, int number,
                                      const ActionId& actionId, const std::string& args,
                                      const std::string& description,
                                      const DeviceId& deviceId) {
    pushUndo();
    BindingState binding;
    binding.id = BindingId{generateId()};
    binding.deviceId = deviceId;
    binding.controlType = controlType;
    binding.channel = channel;
    binding.number = number;
    binding.actionId = actionId;
    binding.args = args;
    binding.description = description;
    state.globalBindings.push_back(std::move(binding));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Binding, state.globalBindings.back().id.str(), "" });
    return state.globalBindings.back().id;
}

void StateAPI::removeBinding(const BindingId& id) {
    pushUndo();
    // Check global bindings first
    auto git = std::find_if(state.globalBindings.begin(), state.globalBindings.end(),
                            [&](auto& b) { return b.id == id; });
    if (git != state.globalBindings.end()) {
        state.globalBindings.erase(git);
        markDirty();
        eventBus.emit({ StateEvent::Deleted, StateEvent::Binding, id.str(), "" });
        return;
    }
    // Then current song
    auto& s = song();
    auto sit = std::find_if(s.bindings.begin(), s.bindings.end(),
                            [&](auto& b) { return b.id == id; });
    if (sit != s.bindings.end()) {
        s.bindings.erase(sit);
        markDirty();
        eventBus.emit({ StateEvent::Deleted, StateEvent::Binding, id.str(), s.id.str() });
    }
}

std::vector<BindingState> StateAPI::bindingsForSong(const SongId& songId) const {
    auto* s = findSong(songId);
    PERF_ASSERT(s, "bindingsForSong: song not found");
    return s->bindings;
}

std::vector<BindingState> StateAPI::globalBindings() const {
    return state.globalBindings;
}

std::vector<BindingState> StateAPI::effectiveBindings() const {
    std::vector<BindingState> result = state.globalBindings;
    auto* s = currentSong();
    if (!s) return result;
    for (auto& sb : s->bindings) {
        result.erase(
            std::remove_if(result.begin(), result.end(),
                [&](auto& gb) {
                    return gb.controlType == sb.controlType &&
                           gb.channel == sb.channel &&
                           gb.number == sb.number;
                }),
            result.end());
        result.push_back(sb);
    }
    return result;
}

// --- Catalog: Plugins ---

// --- Devices ---

DeviceId StateAPI::registerDevice(const std::string& name, const std::string& midiPortName) {
    for (auto& d : state.devices)
        if (d.midiPortName == midiPortName) return d.id;  // dedup
    DeviceState dev;
    dev.id = DeviceId{generateId()};
    dev.name = name;
    dev.midiPortName = midiPortName;
    state.devices.push_back(std::move(dev));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Device, state.devices.back().id.str(), "" });
    return state.devices.back().id;
}

void StateAPI::removeDevice(const DeviceId& id) {
    state.devices.erase(
        std::remove_if(state.devices.begin(), state.devices.end(),
                       [&](auto& d) { return d.id == id; }),
        state.devices.end());
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Device, id.str(), "" });
}

void StateAPI::renameDevice(const DeviceId& id, const std::string& name) {
    device(id).name = name;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Device, id.str(), "" });
}

DeviceState* StateAPI::findDevice(const DeviceId& id) {
    for (auto& d : state.devices)
        if (d.id == id) return &d;
    return nullptr;
}

const DeviceState* StateAPI::findDevice(const DeviceId& id) const {
    for (auto& d : state.devices)
        if (d.id == id) return &d;
    return nullptr;
}

DeviceState* StateAPI::findDeviceByPortName(const std::string& portName) {
    for (auto& d : state.devices)
        if (d.midiPortName == portName) return &d;
    return nullptr;
}

const std::vector<DeviceState>& StateAPI::allDevices() const {
    return state.devices;
}

std::string StateAPI::addDeviceControl(const DeviceId& deviceId, const std::string& name,
                                        const std::string& controlType, int channel, int number,
                                        const std::string& group) {
    auto& dev = device(deviceId);
    for (auto& c : dev.controls)
        if (c.controlType == controlType && c.channel == channel && c.number == number)
            return deviceId.str();  // duplicate
    dev.controls.push_back({ name, controlType, channel, number, group });
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Device, deviceId.str(), "" });
    return deviceId.str();
}

void StateAPI::removeDeviceControl(const DeviceId& deviceId, int index) {
    auto& dev = device(deviceId);
    if (index < 0 || index >= (int)dev.controls.size()) return;  // bounds check
    dev.controls.erase(dev.controls.begin() + index);
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Device, deviceId.str(), "" });
}

void StateAPI::renameDeviceControl(const DeviceId& deviceId, int index, const std::string& name) {
    auto& dev = device(deviceId);
    if (index < 0 || index >= (int)dev.controls.size()) return;  // bounds check
    dev.controls[index].name = name;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Device, deviceId.str(), "" });
}

void StateAPI::setDeviceControlGroup(const DeviceId& deviceId, int index, const std::string& group) {
    auto& dev = device(deviceId);
    if (index < 0 || index >= (int)dev.controls.size()) return;  // bounds check
    dev.controls[index].group = group;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Device, deviceId.str(), "" });
}

void StateAPI::addDeviceToSong(const SongId& songId, const DeviceId& deviceId) {
    auto* s = findSong(songId);
    PERF_ASSERT(s, "addDeviceToSong: song not found");
    for (auto& id : s->deviceIds)
        if (id == deviceId) return;  // already added
    s->deviceIds.push_back(deviceId);
    markDirty();
}

void StateAPI::removeDeviceFromSong(const SongId& songId, const DeviceId& deviceId) {
    auto* s = findSong(songId);
    PERF_ASSERT(s, "removeDeviceFromSong: song not found");
    s->deviceIds.erase(
        std::remove(s->deviceIds.begin(), s->deviceIds.end(), deviceId),
        s->deviceIds.end());
    markDirty();
}

std::vector<DeviceId> StateAPI::devicesForSong(const SongId& songId) const {
    auto* s = findSong(songId);
    PERF_ASSERT(s, "devicesForSong: song not found");
    return s->deviceIds;
}

// --- Score ---

// --- Action Events ---

ActionEventId StateAPI::addActionEvent(double beat, const ActionId& actionId,
                                        const std::string& argsJson) {
    pushUndo();
    auto& s = song();
    SongState::ActionEvent ev;
    ev.id = ActionEventId{generateId()};
    ev.beat = beat;
    ev.actionId = actionId;
    ev.argsJson = argsJson;
    s.actionEvents.push_back(std::move(ev));
    markDirty();
    return s.actionEvents.back().id;
}

void StateAPI::removeActionEvent(const ActionEventId& id) {
    pushUndo();
    auto& s = song();
    auto& events = s.actionEvents;
    events.erase(std::remove_if(events.begin(), events.end(),
        [&](auto& e) { return e.id == id; }), events.end());
    markDirty();
}

void StateAPI::setActionEventBeat(const ActionEventId& id, double beat) {
    pushUndo();
    auto& s = song();
    for (auto& ev : s.actionEvents) {
        if (ev.id == id) { ev.beat = beat; markDirty(); return; }
    }
}

std::vector<SongState::ActionEvent>& StateAPI::actionEvents() {
    return song().actionEvents;  // mutator — must have a song
}

std::vector<BindingState> StateAPI::scoreSteps() const {
    auto* sp = currentSong();
    if (!sp) return {};
    auto& s = *sp;
    std::vector<BindingState> result;
    for (auto& b : s.bindings)
        if (b.isScoreStep) result.push_back(b);
    std::sort(result.begin(), result.end(),
              [](auto& a, auto& b) { return a.scorePosition < b.scorePosition; });
    return result;
}

void StateAPI::setBindingAsScoreStep(const BindingId& bindingId, int position) {
    pushUndo();
    auto& s = song();
    for (auto& b : s.bindings) {
        if (b.id == bindingId) {
            b.isScoreStep = true;
            b.scorePosition = position;
            markDirty();
            eventBus.emit({ StateEvent::Updated, StateEvent::Binding, bindingId.str(), s.id.str() });
            return;
        }
    }
}

void StateAPI::clearScoreStep(const BindingId& bindingId) {
    pushUndo();
    auto& s = song();
    for (auto& b : s.bindings) {
        if (b.id == bindingId) {
            b.isScoreStep = false;
            b.scorePosition = -1;
            markDirty();
            eventBus.emit({ StateEvent::Updated, StateEvent::Binding, bindingId.str(), s.id.str() });
            return;
        }
    }
}

// --- Selection ---

void StateAPI::selectTrack(const TrackId& trackId, bool addToSelection) {
    auto& s = song();
    if (!addToSelection) {
        s.selectedTrackIds.clear();
        s.selectedBusIds.clear();
    }
    auto it = std::find(s.selectedTrackIds.begin(), s.selectedTrackIds.end(), trackId);
    if (it != s.selectedTrackIds.end())
        s.selectedTrackIds.erase(it);
    else
        s.selectedTrackIds.push_back(trackId);
    eventBus.emit({ StateEvent::Updated, StateEvent::Selection, trackId.str(), "" });
}

void StateAPI::selectBus(const BusId& busId, bool addToSelection) {
    auto& s = song();
    if (!addToSelection) {
        s.selectedTrackIds.clear();
        s.selectedBusIds.clear();
    }
    auto it = std::find(s.selectedBusIds.begin(), s.selectedBusIds.end(), busId);
    if (it != s.selectedBusIds.end())
        s.selectedBusIds.erase(it);
    else
        s.selectedBusIds.push_back(busId);
    eventBus.emit({ StateEvent::Updated, StateEvent::Selection, busId.str(), "" });
}

void StateAPI::clearSelection() {
    auto& s = song();
    s.selectedTrackIds.clear();
    s.selectedBusIds.clear();
    eventBus.emit({ StateEvent::Updated, StateEvent::Selection, "", "" });
}

std::vector<TrackId> StateAPI::selectedTrackIds() const {
    auto* s = currentSong();
    return s ? s->selectedTrackIds : std::vector<TrackId>{};
}

std::vector<BusId> StateAPI::selectedBusIds() const {
    auto* s = currentSong();
    return s ? s->selectedBusIds : std::vector<BusId>{};
}

// --- Catalog: Plugins ---

PluginId StateAPI::registerPlugin(const std::string& name, const std::string& manufacturer,
                                   const std::string& formatId, bool isInstrument) {
    for (auto& p : state.plugins)
        if (p.name == name) return p.id;  // dedup
    PluginInfo plugin;
    plugin.id = PluginId{generateId()};
    plugin.name = name;
    plugin.manufacturer = manufacturer;
    plugin.formatId = formatId;
    plugin.isInstrument = isInstrument;
    state.plugins.push_back(std::move(plugin));
    eventBus.emit({ StateEvent::Created, StateEvent::Plugin, state.plugins.back().id.str(), "" });
    return state.plugins.back().id;
}

const PluginInfo* StateAPI::findPluginByName(const std::string& name) const {
    for (auto& p : state.plugins)
        if (p.name == name) return &p;
    return nullptr;
}

const PluginInfo* StateAPI::findPluginById(const PluginId& id) const {
    for (auto& p : state.plugins)
        if (p.id == id) return &p;
    return nullptr;
}

const std::vector<PluginInfo>& StateAPI::allPlugins() const {
    return state.plugins;
}

// --- Catalog: Presets ---

PresetId StateAPI::createPreset(const PluginId& pluginId, const std::string& name,
                                 const std::string& statePath, PresetKind kind) {
    for (auto& p : state.presets)
        if (p.pluginId == pluginId && p.name == name) return p.id;  // dedup
    PresetInfo preset;
    preset.id = PresetId{generateId()};
    preset.pluginId = pluginId;
    preset.name = name;
    preset.statePath = statePath;
    preset.kind = kind;
    state.presets.push_back(std::move(preset));
    eventBus.emit({ StateEvent::Created, StateEvent::Preset, state.presets.back().id.str(), "" });
    return state.presets.back().id;
}

const PresetInfo* StateAPI::findPreset(const PluginId& pluginId, const std::string& name) const {
    for (auto& p : state.presets)
        if (p.pluginId == pluginId && p.name == name) return &p;
    return nullptr;
}

const PresetInfo* StateAPI::findPresetById(const PresetId& id) const {
    for (auto& p : state.presets)
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<const PresetInfo*> StateAPI::presetsForPlugin(const PluginId& pluginId) const {
    std::vector<const PresetInfo*> result;
    for (auto& p : state.presets)
        if (p.pluginId == pluginId) result.push_back(&p);
    return result;
}

// --- Catalog: Actions ---

// Emit the legacy paramSchema JSON for a typed schema, so existing readers
// (ProducePane/SongMappingsPane/MorphEditor/persistence) continue to work
// while they migrate to consuming ActionInfo.params directly.
static std::string legacyParamSchemaJson(const std::vector<ParamSchema>& params) {
    auto legacyType = [](const ParamSchema& p) -> std::string {
        switch (p.type) {
            case ParamType::ChannelRef: {
                // Preserve the old "channel" vs "string" distinction: "channel"
                // when the ref admits bus/master, "string" (track-only) otherwise.
                if (p.scope.empty()) return "channel";
                for (auto& s : p.scope) if (s != "track") return "channel";
                return "string";
            }
            case ParamType::PresetRef: return "string";
            case ParamType::Enum:      return "string";
            case ParamType::Float:     return "float";
            case ParamType::Morph:     return "morph";
        }
        return "string";
    };

    juce::var arr;
    for (auto& p : params) {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("name", juce::String(p.name));
        obj->setProperty("type", juce::String(legacyType(p)));
        arr.append(juce::var(obj));
    }
    return juce::JSON::toString(arr, true).toStdString();
}

ActionId StateAPI::registerAction(const std::string& name, const std::string& label,
                                   std::vector<ParamSchema> params,
                                   int durationParamIndex) {
    auto legacyJson = legacyParamSchemaJson(params);
    for (auto& a : state.actions) {
        if (a.name == name) {
            a.label = label;
            a.params = std::move(params);
            a.paramSchema = legacyJson;
            a.durationParamIndex = durationParamIndex;
            return a.id;
        }
    }
    ActionInfo action;
    action.id = ActionId{generateId()};
    action.name = name;
    action.label = label;
    action.params = std::move(params);
    action.paramSchema = std::move(legacyJson);
    action.durationParamIndex = durationParamIndex;
    state.actions.push_back(std::move(action));
    return state.actions.back().id;
}

ActionId StateAPI::registerAction(const std::string& name, const std::string& label,
                                   const std::string& paramSchema, int durationParamIndex) {
    for (auto& a : state.actions) {
        if (a.name == name) {
            a.label = label;
            a.paramSchema = paramSchema;
            a.params.clear();  // legacy path — no typed schema
            a.durationParamIndex = durationParamIndex;
            return a.id;
        }
    }
    ActionInfo action;
    action.id = ActionId{generateId()};
    action.name = name;
    action.label = label;
    action.paramSchema = paramSchema;
    action.durationParamIndex = durationParamIndex;
    state.actions.push_back(std::move(action));
    return state.actions.back().id;
}

ActionId StateAPI::createCustomAction(const std::string& name, const std::string& label,
                                       const std::string& luaCode, const SongId& songId) {
    for (auto& a : state.actions) {
        if (a.name == name) {
            a.label = label;
            a.luaCode = luaCode;
            a.songId = songId;
            markDirty();
            return a.id;
        }
    }
    ActionInfo action;
    action.id = ActionId{generateId()};
    action.name = name;
    action.label = label;
    action.luaCode = luaCode;
    action.songId = songId;
    state.actions.push_back(std::move(action));
    markDirty();
    return state.actions.back().id;
}

void StateAPI::removeAction(const ActionId& id) {
    auto& actions = state.actions;
    actions.erase(std::remove_if(actions.begin(), actions.end(),
        [&](const ActionInfo& a) { return a.id == id; }), actions.end());
    markDirty();
}

const ActionInfo* StateAPI::findActionByName(const std::string& name) const {
    for (auto& a : state.actions)
        if (a.name == name) return &a;
    return nullptr;
}

const ActionInfo* StateAPI::findActionById(const ActionId& id) const {
    for (auto& a : state.actions)
        if (a.id == id) return &a;
    return nullptr;
}

const std::vector<ActionInfo>& StateAPI::allActions() const {
    return state.actions;
}

// --- Config ---

void StateAPI::setConfig(const std::string& key, const std::string& value) {
    state.config[key] = value;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Config, key, "" });
}

std::string StateAPI::getConfig(const std::string& key, const std::string& defaultValue) const {
    auto it = state.config.find(key);
    return it != state.config.end() ? it->second : defaultValue;
}

// --- Name resolution ---

TrackId StateAPI::findTrackIdByName(const std::string& name) const {
    auto* s = currentSong();
    if (!s) return {};
    for (auto& t : s->tracks)
        if (t.name == name) return t.id;
    return {};
}

BusId StateAPI::findBusIdByName(const std::string& name) const {
    auto* s = currentSong();
    if (!s) return {};
    for (auto& b : s->busses)
        if (b.name == name) return b.id;
    return {};
}

// --- Query helpers ---

std::vector<StateAPI::TrackInfo> StateAPI::listTracks() const {
    std::vector<TrackInfo> result;
    auto* s = currentSong();
    if (!s) return result;
    std::vector<const TrackState*> sorted;
    for (auto& t : s->tracks) sorted.push_back(&t);
    std::sort(sorted.begin(), sorted.end(),
              [](auto* a, auto* b) { return a->position < b->position; });
    for (auto* t : sorted)
        result.push_back({ t->id, t->name });
    return result;
}

std::vector<StateAPI::BusInfo> StateAPI::listBusses() const {
    std::vector<BusInfo> result;
    auto* s = currentSong();
    if (!s) return result;
    for (auto& b : s->busses)
        result.push_back({ b.id, b.name });
    return result;
}

std::string StateAPI::getTrackPluginName(const TrackId& trackId) const {
    auto& t = track(trackId);
    if (t.pluginId.empty()) return {};
    auto* plugin = findPluginById(t.pluginId);
    return plugin ? plugin->name : std::string{};
}

std::vector<StateAPI::EffectSlotInfo> StateAPI::getTrackEffects(const TrackId& trackId) const {
    std::vector<EffectSlotInfo> result;
    auto& t = track(trackId);
    for (auto& fx : t.effects) {
        auto* plugin = findPluginById(fx.pluginId);
        result.push_back({ fx.id, plugin ? plugin->name : fx.name });
    }
    return result;
}

std::vector<StateAPI::EffectSlotInfo> StateAPI::getBusEffects(const BusId& busId) const {
    std::vector<EffectSlotInfo> result;
    auto& b = bus(busId);
    for (auto& fx : b.effects) {
        auto* plugin = findPluginById(fx.pluginId);
        result.push_back({ fx.id, plugin ? plugin->name : fx.name });
    }
    return result;
}

std::vector<StateAPI::EffectSlotInfo> StateAPI::getMasterEffects() const {
    std::vector<EffectSlotInfo> result;
    auto* s = currentSong();
    if (!s) return result;
    for (auto& fx : s->masterEffects) {
        auto* plugin = findPluginById(fx.pluginId);
        result.push_back({ fx.id, plugin ? plugin->name : fx.name });
    }
    return result;
}

std::vector<StateAPI::TrackSendInfo> StateAPI::getTrackSends(const TrackId& trackId) const {
    std::vector<TrackSendInfo> result;
    auto& t = track(trackId);
    for (auto& send : t.sends) {
        std::string busName;
        auto* b = findBus(send.busId);
        if (b) busName = b->name;
        result.push_back({ busName, send.busId, send.gain });
    }
    return result;
}

// --- Events ---

StateEventBus& StateAPI::events() {
    return eventBus;
}

// --- Dirty tracking ---

void StateAPI::replaceState(AppState&& newState) {
    state = std::move(newState);
    dirty = false;
    if (!state.currentSongId.empty())
        eventBus.emit({ StateEvent::Updated, StateEvent::Config, "current_song_id", "" });
}

void StateAPI::pushUndo() {
    if (inTransaction) return;
    undoHistory.push(state);
}

void StateAPI::beginTransaction() {
    if (!inTransaction) {
        undoHistory.push(state);
        inTransaction = true;
    }
}

void StateAPI::endTransaction() {
    inTransaction = false;
}

bool StateAPI::undo() {
    if (!undoHistory.canUndo()) return false;
    auto restored = undoHistory.undo(state);
    state = std::move(restored);
    dirty = true;
    if (!state.currentSongId.empty())
        eventBus.emit({ StateEvent::Updated, StateEvent::Config, "current_song_id", "" });
    return true;
}

bool StateAPI::redo() {
    if (!undoHistory.canRedo()) return false;
    auto restored = undoHistory.redo(state);
    state = std::move(restored);
    dirty = true;
    if (!state.currentSongId.empty())
        eventBus.emit({ StateEvent::Updated, StateEvent::Config, "current_song_id", "" });
    return true;
}

void StateAPI::markDirty() {
    dirty = true;
}

bool StateAPI::isDirty() const {
    return dirty;
}

void StateAPI::clearDirty() {
    dirty = false;
}
