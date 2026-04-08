#include "api/StateAPI.h"
#include "engine/Log.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

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

// --- Helpers ---

SongState* StateAPI::currentSong() {
    if (state.currentSongId.empty()) return nullptr;
    return findSong(state.currentSongId);
}

const SongState* StateAPI::currentSong() const {
    if (state.currentSongId.empty()) return nullptr;
    return findSong(state.currentSongId);
}

SongState* StateAPI::findSong(const std::string& id) {
    for (auto& s : state.songs)
        if (s.id == id) return &s;
    return nullptr;
}

const SongState* StateAPI::findSong(const std::string& id) const {
    for (auto& s : state.songs)
        if (s.id == id) return &s;
    return nullptr;
}

TrackState* StateAPI::findTrack(const std::string& id) {
    auto* song = currentSong();
    if (!song) return nullptr;
    for (auto& t : song->tracks)
        if (t.id == id) return &t;
    return nullptr;
}

const TrackState* StateAPI::findTrack(const std::string& id) const {
    auto* song = currentSong();
    if (!song) return nullptr;
    for (auto& t : song->tracks)
        if (t.id == id) return &t;
    return nullptr;
}

BusState* StateAPI::findBus(const std::string& id) {
    auto* song = currentSong();
    if (!song) return nullptr;
    for (auto& b : song->busses)
        if (b.id == id) return &b;
    return nullptr;
}

const BusState* StateAPI::findBus(const std::string& id) const {
    auto* song = currentSong();
    if (!song) return nullptr;
    for (auto& b : song->busses)
        if (b.id == id) return &b;
    return nullptr;
}

std::vector<EffectState>* StateAPI::findEffectList(const std::string& effectId, std::string* outParentId) {
    auto* song = currentSong();
    if (!song) return nullptr;

    // Master effects
    for (auto& fx : song->masterEffects) {
        if (fx.id == effectId) {
            if (outParentId) *outParentId = song->id;
            return &song->masterEffects;
        }
    }
    // Track effects
    for (auto& t : song->tracks) {
        for (auto& fx : t.effects) {
            if (fx.id == effectId) {
                if (outParentId) *outParentId = t.id;
                return &t.effects;
            }
        }
    }
    // Bus effects
    for (auto& b : song->busses) {
        for (auto& fx : b.effects) {
            if (fx.id == effectId) {
                if (outParentId) *outParentId = b.id;
                return &b.effects;
            }
        }
    }
    return nullptr;
}

std::vector<SendState>* StateAPI::findSendList(const std::string& sendId, std::string* outTrackId) {
    auto* song = currentSong();
    if (!song) return nullptr;
    for (auto& t : song->tracks) {
        for (auto& s : t.sends) {
            if (s.id == sendId) {
                if (outTrackId) *outTrackId = t.id;
                return &t.sends;
            }
        }
    }
    return nullptr;
}

const EffectState* StateAPI::findEffect(const std::string& effectId) const {
    auto* song = currentSong();
    if (!song) return nullptr;
    for (auto& fx : song->masterEffects)
        if (fx.id == effectId) return &fx;
    for (auto& t : song->tracks)
        for (auto& fx : t.effects)
            if (fx.id == effectId) return &fx;
    for (auto& b : song->busses)
        for (auto& fx : b.effects)
            if (fx.id == effectId) return &fx;
    return nullptr;
}

// --- Song ---

std::string StateAPI::createSong(const std::string& name) {
    SongState song;
    song.id = generateId();
    song.name = name;
    state.songs.push_back(std::move(song));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Song, state.songs.back().id, "" });
    return state.songs.back().id;
}

void StateAPI::deleteSong(const std::string& id) {
    auto it = std::find_if(state.songs.begin(), state.songs.end(),
                           [&](auto& s) { return s.id == id; });
    if (it == state.songs.end()) return;
    std::string songId = it->id;
    state.songs.erase(it);
    if (state.currentSongId == songId)
        state.currentSongId.clear();
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Song, songId, "" });
}

const std::vector<SongState>& StateAPI::allSongs() const {
    return state.songs;
}

void StateAPI::setCurrentSong(const std::string& songId) {
    if (state.currentSongId == songId) return;
    state.currentSongId = songId;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Config, "current_song_id", "" });
}

void StateAPI::setMasterGain(float gain) {
    auto* song = currentSong();
    if (!song) return;
    song->masterGain = gain;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Song, song->id, "" });
}

float StateAPI::getMasterGain() const {
    auto* song = currentSong();
    return song ? song->masterGain : 1.0f;
}

std::string StateAPI::getMasterOutputId() const {
    return state.currentSongId;
}

// --- Tracks ---

std::string StateAPI::createTrack(const std::string& name) {
    auto* song = currentSong();
    if (!song) return {};
    TrackState track;
    track.id = generateId();
    track.name = name;
    track.position = (int)song->tracks.size();
    song->tracks.push_back(std::move(track));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Track, song->tracks.back().id, "" });
    return song->tracks.back().id;
}

std::string StateAPI::createAudioInputTrack(const std::string& name, int inputChannelStart,
                                             int inputChannelCount) {
    auto* song = currentSong();
    if (!song) return {};
    TrackState track;
    track.id = generateId();
    track.name = name;
    track.position = (int)song->tracks.size();
    track.sourceType = TrackSourceType::AudioInput;
    track.channelMode = (inputChannelCount == 1) ? ChannelMode::Mono : ChannelMode::Stereo;
    track.inputChannelStart = inputChannelStart;
    track.inputChannelCount = inputChannelCount;
    track.midiEnabled = false;  // audio input tracks don't receive MIDI
    song->tracks.push_back(std::move(track));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Track, song->tracks.back().id, "" });
    return song->tracks.back().id;
}

void StateAPI::removeTrack(const std::string& id) {
    auto* song = currentSong();
    if (!song) return;
    auto it = std::find_if(song->tracks.begin(), song->tracks.end(),
                           [&](auto& t) { return t.id == id; });
    if (it == song->tracks.end()) return;
    song->tracks.erase(it);
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Track, id, "" });
}

void StateAPI::renameTrack(const std::string& id, const std::string& name) {
    auto* track = findTrack(id);
    if (!track) return;
    track->name = name;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id, "" });
}

void StateAPI::setTrackGain(const std::string& id, float gain) {
    auto* track = findTrack(id);
    if (!track) return;
    track->outputGain = gain;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id, "" });
}

float StateAPI::getTrackGain(const std::string& id) const {
    auto* track = findTrack(id);
    return track ? track->outputGain : 1.0f;
}

void StateAPI::setTrackMidiEnabled(const std::string& id, bool enabled) {
    auto* track = findTrack(id);
    if (!track) return;
    track->midiEnabled = enabled;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id, "" });
}

bool StateAPI::isTrackMidiEnabled(const std::string& id) const {
    auto* track = findTrack(id);
    return track ? track->midiEnabled : true;
}

void StateAPI::setTrackAudioEnabled(const std::string& id, bool enabled) {
    auto* track = findTrack(id);
    if (!track) return;
    track->audioEnabled = enabled;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id, "" });
}

bool StateAPI::isTrackAudioEnabled(const std::string& id) const {
    auto* track = findTrack(id);
    return track ? track->audioEnabled : true;
}

void StateAPI::setTrackPlugin(const std::string& id, const std::string& pluginId,
                               const std::string& presetId) {
    auto* track = findTrack(id);
    if (!track) return;
    track->pluginId = pluginId;
    track->presetId = presetId;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id, "" });
}

void StateAPI::clearTrackPlugin(const std::string& id) {
    setTrackPlugin(id, "", "");
}

void StateAPI::setTrackInputChannels(const std::string& id, int start, int count) {
    auto* track = findTrack(id);
    if (!track) return;
    track->inputChannelStart = start;
    track->inputChannelCount = count;
    track->channelMode = (count == 1) ? ChannelMode::Mono : ChannelMode::Stereo;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id, "" });
}

void StateAPI::setTrackInstrumentLoadStatus(const std::string& id, LoadStatus status) {
    auto* track = findTrack(id);
    if (!track) return;
    track->instrumentLoadStatus = status;
    eventBus.emit({ StateEvent::Updated, StateEvent::Track, id, "" });
}

void StateAPI::setEffectLoadStatus(const std::string& effectId, LoadStatus status) {
    std::string parentId;
    auto* list = findEffectList(effectId, &parentId);
    if (!list) return;
    for (auto& fx : *list) {
        if (fx.id == effectId) {
            fx.loadStatus = status;
            eventBus.emit({ StateEvent::Updated, StateEvent::Effect, effectId, parentId });
            return;
        }
    }
}

// --- Busses ---

std::string StateAPI::createBus(const std::string& name) {
    auto* song = currentSong();
    if (!song) return {};
    BusState bus;
    bus.id = generateId();
    bus.name = name;
    bus.position = (int)song->busses.size();
    song->busses.push_back(std::move(bus));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Bus, song->busses.back().id, "" });
    return song->busses.back().id;
}

void StateAPI::removeBus(const std::string& id) {
    auto* song = currentSong();
    if (!song) return;
    auto it = std::find_if(song->busses.begin(), song->busses.end(),
                           [&](auto& b) { return b.id == id; });
    if (it == song->busses.end()) return;
    song->busses.erase(it);
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Bus, id, "" });
}

void StateAPI::renameBus(const std::string& id, const std::string& name) {
    auto* bus = findBus(id);
    if (!bus) return;
    bus->name = name;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Bus, id, "" });
}

void StateAPI::setBusGain(const std::string& id, float gain) {
    auto* bus = findBus(id);
    if (!bus) return;
    bus->outputGain = gain;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Bus, id, "" });
}

float StateAPI::getBusGain(const std::string& id) const {
    auto* bus = findBus(id);
    return bus ? bus->outputGain : 1.0f;
}

// --- Effects ---

std::string StateAPI::addEffect(const std::string& parentId, const std::string& name,
                                 const std::string& pluginId) {
    auto* song = currentSong();
    if (!song) return {};

    EffectState effect;
    effect.id = generateId();
    effect.name = name;
    effect.pluginId = pluginId;

    std::vector<EffectState>* list = nullptr;

    // Master effects (parent = songId)
    if (parentId == song->id) {
        list = &song->masterEffects;
    } else {
        // Track or bus
        for (auto& t : song->tracks) {
            if (t.id == parentId) { list = &t.effects; break; }
        }
        if (!list) {
            for (auto& b : song->busses) {
                if (b.id == parentId) { list = &b.effects; break; }
            }
        }
    }

    if (!list) return {};
    effect.position = (int)list->size();
    list->push_back(std::move(effect));
    auto& added = list->back();
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Effect, added.id, parentId });
    return added.id;
}

void StateAPI::removeEffect(const std::string& effectId) {
    std::string parentId;
    auto* list = findEffectList(effectId, &parentId);
    if (!list) return;
    list->erase(std::remove_if(list->begin(), list->end(),
                               [&](auto& fx) { return fx.id == effectId; }),
                list->end());
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Effect, effectId, parentId });
}

// --- Sends ---

std::string StateAPI::addSend(const std::string& trackId, const std::string& busId, float gain) {
    auto* track = findTrack(trackId);
    if (!track) return {};
    SendState send;
    send.id = generateId();
    send.busId = busId;
    send.gain = gain;
    track->sends.push_back(std::move(send));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Send, track->sends.back().id, trackId });
    return track->sends.back().id;
}

void StateAPI::removeSend(const std::string& sendId) {
    std::string trackId;
    auto* list = findSendList(sendId, &trackId);
    if (!list) return;
    list->erase(std::remove_if(list->begin(), list->end(),
                               [&](auto& s) { return s.id == sendId; }),
                list->end());
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Send, sendId, trackId });
}

void StateAPI::setSendGainByBus(const std::string& trackId, const std::string& busId, float gain) {
    auto* track = findTrack(trackId);
    if (!track) return;
    for (auto& s : track->sends) {
        if (s.busId == busId) {
            setSendGain(s.id, gain);
            return;
        }
    }
}

void StateAPI::setSendGain(const std::string& sendId, float gain) {
    auto* song = currentSong();
    if (!song) return;
    for (auto& t : song->tracks) {
        for (auto& s : t.sends) {
            if (s.id == sendId) {
                s.gain = gain;
                markDirty();
                eventBus.emit({ StateEvent::Updated, StateEvent::Send, sendId, t.id });
                return;
            }
        }
    }
}

// --- Bindings ---

std::string StateAPI::addBinding(const std::string& songId, const std::string& controlType,
                                  int channel, int number, const std::string& actionId,
                                  const std::string& args, const std::string& description,
                                  const std::string& deviceId) {
    auto* song = findSong(songId);
    if (!song) return {};
    BindingState binding;
    binding.id = generateId();
    binding.songId = songId;
    binding.deviceId = deviceId;
    binding.controlType = controlType;
    binding.channel = channel;
    binding.number = number;
    binding.actionId = actionId;
    binding.args = args;
    binding.description = description;
    song->bindings.push_back(std::move(binding));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Binding, song->bindings.back().id, songId });
    return song->bindings.back().id;
}

std::string StateAPI::addGlobalBinding(const std::string& controlType, int channel, int number,
                                        const std::string& actionId, const std::string& args,
                                        const std::string& description,
                                        const std::string& deviceId) {
    BindingState binding;
    binding.id = generateId();
    // songId left empty = global
    binding.deviceId = deviceId;
    binding.controlType = controlType;
    binding.channel = channel;
    binding.number = number;
    binding.actionId = actionId;
    binding.args = args;
    binding.description = description;
    state.globalBindings.push_back(std::move(binding));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Binding, state.globalBindings.back().id, "" });
    return state.globalBindings.back().id;
}

void StateAPI::removeBinding(const std::string& id) {
    // Check global bindings first
    auto git = std::find_if(state.globalBindings.begin(), state.globalBindings.end(),
                            [&](auto& b) { return b.id == id; });
    if (git != state.globalBindings.end()) {
        state.globalBindings.erase(git);
        markDirty();
        eventBus.emit({ StateEvent::Deleted, StateEvent::Binding, id, "" });
        return;
    }
    // Then check current song
    auto* song = currentSong();
    if (!song) return;
    auto sit = std::find_if(song->bindings.begin(), song->bindings.end(),
                            [&](auto& b) { return b.id == id; });
    if (sit != song->bindings.end()) {
        song->bindings.erase(sit);
        markDirty();
        eventBus.emit({ StateEvent::Deleted, StateEvent::Binding, id, song->id });
    }
}

std::vector<BindingState> StateAPI::bindingsForSong(const std::string& songId) const {
    auto* song = findSong(songId);
    if (!song) return {};
    return song->bindings;
}

std::vector<BindingState> StateAPI::globalBindings() const {
    return state.globalBindings;
}

std::vector<BindingState> StateAPI::effectiveBindings() const {
    // Start with global bindings
    std::vector<BindingState> result = state.globalBindings;

    // Song bindings override globals on same control
    auto* song = currentSong();
    if (song) {
        for (auto& sb : song->bindings) {
            // Remove any global binding on the same control
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
    }
    return result;
}

// --- Catalog: Plugins ---

// --- Devices ---

std::string StateAPI::registerDevice(const std::string& name, const std::string& midiPortName) {
    for (auto& d : state.devices)
        if (d.midiPortName == midiPortName) return d.id;
    DeviceState device;
    device.id = generateId();
    device.name = name;
    device.midiPortName = midiPortName;
    state.devices.push_back(std::move(device));
    markDirty();
    eventBus.emit({ StateEvent::Created, StateEvent::Device, state.devices.back().id, "" });
    return state.devices.back().id;
}

void StateAPI::removeDevice(const std::string& id) {
    state.devices.erase(
        std::remove_if(state.devices.begin(), state.devices.end(),
                       [&](auto& d) { return d.id == id; }),
        state.devices.end());
    markDirty();
    eventBus.emit({ StateEvent::Deleted, StateEvent::Device, id, "" });
}

void StateAPI::renameDevice(const std::string& id, const std::string& name) {
    auto* device = findDevice(id);
    if (!device) return;
    device->name = name;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Device, id, "" });
}

DeviceState* StateAPI::findDevice(const std::string& id) {
    for (auto& d : state.devices)
        if (d.id == id) return &d;
    return nullptr;
}

const DeviceState* StateAPI::findDevice(const std::string& id) const {
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

std::string StateAPI::addDeviceControl(const std::string& deviceId, const std::string& name,
                                        const std::string& controlType, int channel, int number,
                                        const std::string& group) {
    auto* device = findDevice(deviceId);
    if (!device) return {};
    device->controls.push_back({ name, controlType, channel, number, group });
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Device, deviceId, "" });
    return deviceId;
}

void StateAPI::removeDeviceControl(const std::string& deviceId, int index) {
    auto* device = findDevice(deviceId);
    if (!device || index < 0 || index >= (int)device->controls.size()) return;
    device->controls.erase(device->controls.begin() + index);
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Device, deviceId, "" });
}

void StateAPI::renameDeviceControl(const std::string& deviceId, int index, const std::string& name) {
    auto* device = findDevice(deviceId);
    if (!device || index < 0 || index >= (int)device->controls.size()) return;
    device->controls[index].name = name;
    markDirty();
    eventBus.emit({ StateEvent::Updated, StateEvent::Device, deviceId, "" });
}

void StateAPI::addDeviceToSong(const std::string& songId, const std::string& deviceId) {
    auto* song = findSong(songId);
    if (!song) return;
    for (auto& id : song->deviceIds)
        if (id == deviceId) return;  // already added
    song->deviceIds.push_back(deviceId);
    markDirty();
}

void StateAPI::removeDeviceFromSong(const std::string& songId, const std::string& deviceId) {
    auto* song = findSong(songId);
    if (!song) return;
    song->deviceIds.erase(
        std::remove(song->deviceIds.begin(), song->deviceIds.end(), deviceId),
        song->deviceIds.end());
    markDirty();
}

std::vector<std::string> StateAPI::devicesForSong(const std::string& songId) const {
    auto* song = findSong(songId);
    return song ? song->deviceIds : std::vector<std::string>{};
}

// --- Score ---

std::vector<BindingState> StateAPI::scoreSteps() const {
    auto* song = currentSong();
    if (!song) return {};
    std::vector<BindingState> result;
    for (auto& b : song->bindings)
        if (b.isScoreStep) result.push_back(b);
    std::sort(result.begin(), result.end(),
              [](auto& a, auto& b) { return a.scorePosition < b.scorePosition; });
    return result;
}

void StateAPI::setBindingAsScoreStep(const std::string& bindingId, int position) {
    auto* song = currentSong();
    if (!song) return;
    for (auto& b : song->bindings) {
        if (b.id == bindingId) {
            b.isScoreStep = true;
            b.scorePosition = position;
            markDirty();
            eventBus.emit({ StateEvent::Updated, StateEvent::Binding, bindingId, song->id });
            return;
        }
    }
}

void StateAPI::clearScoreStep(const std::string& bindingId) {
    auto* song = currentSong();
    if (!song) return;
    for (auto& b : song->bindings) {
        if (b.id == bindingId) {
            b.isScoreStep = false;
            b.scorePosition = -1;
            markDirty();
            eventBus.emit({ StateEvent::Updated, StateEvent::Binding, bindingId, song->id });
            return;
        }
    }
}

// --- Selection ---

void StateAPI::selectTrack(const std::string& trackId, bool addToSelection) {
    auto* song = currentSong();
    if (!song) return;
    if (!addToSelection) {
        song->selectedTrackIds.clear();
        song->selectedBusIds.clear();
    }
    // Toggle if already selected
    auto it = std::find(song->selectedTrackIds.begin(), song->selectedTrackIds.end(), trackId);
    if (it != song->selectedTrackIds.end())
        song->selectedTrackIds.erase(it);
    else
        song->selectedTrackIds.push_back(trackId);
    eventBus.emit({ StateEvent::Updated, StateEvent::Selection, trackId, "" });
}

void StateAPI::selectBus(const std::string& busId, bool addToSelection) {
    auto* song = currentSong();
    if (!song) return;
    if (!addToSelection) {
        song->selectedTrackIds.clear();
        song->selectedBusIds.clear();
    }
    auto it = std::find(song->selectedBusIds.begin(), song->selectedBusIds.end(), busId);
    if (it != song->selectedBusIds.end())
        song->selectedBusIds.erase(it);
    else
        song->selectedBusIds.push_back(busId);
    eventBus.emit({ StateEvent::Updated, StateEvent::Selection, busId, "" });
}

void StateAPI::clearSelection() {
    auto* song = currentSong();
    if (!song) return;
    song->selectedTrackIds.clear();
    song->selectedBusIds.clear();
    eventBus.emit({ StateEvent::Updated, StateEvent::Selection, "", "" });
}

std::vector<std::string> StateAPI::selectedTrackIds() const {
    auto* song = currentSong();
    return song ? song->selectedTrackIds : std::vector<std::string>{};
}

std::vector<std::string> StateAPI::selectedBusIds() const {
    auto* song = currentSong();
    return song ? song->selectedBusIds : std::vector<std::string>{};
}

// --- Catalog: Plugins ---

std::string StateAPI::registerPlugin(const std::string& name, const std::string& manufacturer,
                                      const std::string& formatId, bool isInstrument) {
    // Deduplicate by name
    for (auto& p : state.plugins)
        if (p.name == name) return p.id;
    PluginInfo plugin;
    plugin.id = generateId();
    plugin.name = name;
    plugin.manufacturer = manufacturer;
    plugin.formatId = formatId;
    plugin.isInstrument = isInstrument;
    state.plugins.push_back(std::move(plugin));
    eventBus.emit({ StateEvent::Created, StateEvent::Plugin, state.plugins.back().id, "" });
    return state.plugins.back().id;
}

const PluginInfo* StateAPI::findPluginByName(const std::string& name) const {
    for (auto& p : state.plugins)
        if (p.name == name) return &p;
    return nullptr;
}

const PluginInfo* StateAPI::findPluginById(const std::string& id) const {
    for (auto& p : state.plugins)
        if (p.id == id) return &p;
    return nullptr;
}

const std::vector<PluginInfo>& StateAPI::allPlugins() const {
    return state.plugins;
}

// --- Catalog: Presets ---

std::string StateAPI::createPreset(const std::string& pluginId, const std::string& name,
                                    const std::string& statePath, PresetKind kind) {
    // Deduplicate by pluginId + name
    for (auto& p : state.presets)
        if (p.pluginId == pluginId && p.name == name) return p.id;
    PresetInfo preset;
    preset.id = generateId();
    preset.pluginId = pluginId;
    preset.name = name;
    preset.statePath = statePath;
    preset.kind = kind;
    state.presets.push_back(std::move(preset));
    eventBus.emit({ StateEvent::Created, StateEvent::Preset, state.presets.back().id, "" });
    return state.presets.back().id;
}

const PresetInfo* StateAPI::findPreset(const std::string& pluginId, const std::string& name) const {
    for (auto& p : state.presets)
        if (p.pluginId == pluginId && p.name == name) return &p;
    return nullptr;
}

const PresetInfo* StateAPI::findPresetById(const std::string& id) const {
    for (auto& p : state.presets)
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<const PresetInfo*> StateAPI::presetsForPlugin(const std::string& pluginId) const {
    std::vector<const PresetInfo*> result;
    for (auto& p : state.presets)
        if (p.pluginId == pluginId) result.push_back(&p);
    return result;
}

// --- Catalog: Actions ---

std::string StateAPI::registerAction(const std::string& name, const std::string& label,
                                      const std::string& paramSchema) {
    for (auto& a : state.actions)
        if (a.name == name) return a.id;
    ActionInfo action;
    action.id = generateId();
    action.name = name;
    action.label = label;
    action.paramSchema = paramSchema;
    state.actions.push_back(std::move(action));
    return state.actions.back().id;
}

const ActionInfo* StateAPI::findActionByName(const std::string& name) const {
    for (auto& a : state.actions)
        if (a.name == name) return &a;
    return nullptr;
}

const ActionInfo* StateAPI::findActionById(const std::string& id) const {
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

std::string StateAPI::findTrackIdByName(const std::string& name) const {
    auto* song = currentSong();
    if (!song) return {};
    for (auto& t : song->tracks)
        if (t.name == name) return t.id;
    return {};
}

std::string StateAPI::findBusIdByName(const std::string& name) const {
    auto* song = currentSong();
    if (!song) return {};
    for (auto& b : song->busses)
        if (b.name == name) return b.id;
    return {};
}

// --- Query helpers ---

std::vector<StateAPI::TrackInfo> StateAPI::listTracks() const {
    std::vector<TrackInfo> result;
    auto* song = currentSong();
    if (!song) return result;
    for (auto& t : song->tracks)
        result.push_back({ t.id, t.name });
    return result;
}

std::vector<StateAPI::BusInfo> StateAPI::listBusses() const {
    std::vector<BusInfo> result;
    auto* song = currentSong();
    if (!song) return result;
    for (auto& b : song->busses)
        result.push_back({ b.id, b.name });
    return result;
}

std::string StateAPI::getTrackPluginName(const std::string& trackId) const {
    auto* track = findTrack(trackId);
    if (!track || track->pluginId.empty()) return {};
    auto* plugin = findPluginById(track->pluginId);
    return plugin ? plugin->name : std::string{};
}

std::vector<StateAPI::EffectSlotInfo> StateAPI::getTrackEffects(const std::string& trackId) const {
    std::vector<EffectSlotInfo> result;
    auto* track = findTrack(trackId);
    if (!track) return result;
    for (auto& fx : track->effects) {
        auto* plugin = findPluginById(fx.pluginId);
        result.push_back({ fx.id, plugin ? plugin->name : fx.name });
    }
    return result;
}

std::vector<StateAPI::EffectSlotInfo> StateAPI::getBusEffects(const std::string& busId) const {
    std::vector<EffectSlotInfo> result;
    auto* bus = findBus(busId);
    if (!bus) return result;
    for (auto& fx : bus->effects) {
        auto* plugin = findPluginById(fx.pluginId);
        result.push_back({ fx.id, plugin ? plugin->name : fx.name });
    }
    return result;
}

std::vector<StateAPI::EffectSlotInfo> StateAPI::getMasterEffects() const {
    std::vector<EffectSlotInfo> result;
    auto* song = currentSong();
    if (!song) return result;
    for (auto& fx : song->masterEffects) {
        auto* plugin = findPluginById(fx.pluginId);
        result.push_back({ fx.id, plugin ? plugin->name : fx.name });
    }
    return result;
}

std::vector<StateAPI::TrackSendInfo> StateAPI::getTrackSends(const std::string& trackId) const {
    std::vector<TrackSendInfo> result;
    auto* track = findTrack(trackId);
    if (!track) return result;
    for (auto& send : track->sends) {
        std::string busName;
        auto* bus = findBus(send.busId);
        if (bus) busName = bus->name;
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
    // Single event: tell EngineSync to rebuild for the current song
    if (!state.currentSongId.empty())
        eventBus.emit({ StateEvent::Updated, StateEvent::Config, "current_song_id", "" });
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
