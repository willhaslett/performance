#include "api/PerformanceAPI.h"
#include "automation/AutomationEngine.h"
#include "engine/AudioEngine.h"
#include "engine/EngineSync.h"
#include "engine/MIDIEngine.h"
#include "engine/Log.h"
#include "registry/Registry.h"
#include "song/Song.h"
#include "song/SongRuntime.h"

PerformanceAPI::PerformanceAPI() {}

PerformanceAPI::~PerformanceAPI() {
    shutdown();
}

void PerformanceAPI::initialise() {
    // Open registry
    registry = std::make_unique<Registry>();
    auto configDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                         .getChildFile(".config/performance");
    configDir.createDirectory();
    registry->open(configDir.getChildFile("registry.db").getFullPathName().toStdString());

    audioEngine = std::make_unique<AudioEngine>();
    audioEngine->initialise();
    perfLog("[API] AudioEngine initialised\n");

    // Populate plugins table from scan cache
    populatePluginRegistry();

    // Register built-in actions
    registerBuiltinActions();

    // Engine sync — registry events drive the engine
    engineSync = std::make_unique<EngineSync>(*audioEngine, *registry);

    automationEngine = std::make_unique<AutomationEngine>();
    songRuntime = std::make_unique<SongRuntime>(*audioEngine);

    midiEngine = std::make_unique<MIDIEngine>(
        audioEngine->getDeviceManager(), *audioEngine);
    midiEngine->setSongRuntime(songRuntime.get());
    midiEngine->setMonitorMode(true);
    midiEngine->initialise();
    perfLog("[API] MIDIEngine initialised\n");
}

void PerformanceAPI::shutdown() {
    songRuntime.reset();
    midiEngine.reset();
    engineSync.reset();
    audioEngine.reset();
    automationEngine.reset();
    registry.reset();
}

// --- Track management ---

void PerformanceAPI::createTrack(const juce::String& name) {
    // No-op when song is active — track creation happens in addInstrument via registry + sync
    if (currentSongId.empty())
        audioEngine->createTrack(name);
}

void PerformanceAPI::removeTrack(const juce::String& name) {
    audioEngine->removeTrack(name);
}

void PerformanceAPI::addInstrument(const juce::String& trackName, const juce::String& pluginName,
                                    const juce::String& snapshotName) {
    auto plugin = registry->findPluginByName(pluginName.toStdString());
    if (!plugin) {
        perfLog("[API] Plugin not found: %s\n", pluginName.toRawUTF8());
        return;
    }

    std::string snapshotId;
    if (snapshotName.isNotEmpty()) {
        auto snap = registry->findSnapshot(plugin->id, snapshotName.toStdString());
        if (snap) snapshotId = snap->id;
    }

    if (!currentSongId.empty()) {
        registry->createTrack(currentSongId, trackName.toStdString(), plugin->id, snapshotId);
        engineSync->sync(currentSongId);
    } else {
        audioEngine->addTrackInstrument(trackName, pluginName, nullptr);
    }
}

void PerformanceAPI::addTrackEffect(const juce::String& trackName, const juce::String& effectName,
                                     const juce::String& pluginName) {
    if (!currentSongId.empty()) {
        auto plugin = registry->findPluginByName(pluginName.toStdString());
        if (plugin) {
            for (auto& t : registry->tracksForSong(currentSongId)) {
                if (t.name == trackName.toStdString()) {
                    registry->createEffect(t.id, EntityType::Track,
                                           effectName.toStdString(), plugin->id);
                    break;
                }
            }
        }
        engineSync->sync(currentSongId);
    } else {
        audioEngine->addTrackEffect(trackName, effectName, pluginName);
    }
}

void PerformanceAPI::setTrackMidiEnabled(const juce::String& trackName, bool enabled) {
    audioEngine->setTrackMidiEnabled(trackName, enabled);
}

void PerformanceAPI::setTrackGain(const juce::String& trackName, float gain) {
    audioEngine->setTrackGain(trackName, gain);
}

float PerformanceAPI::getTrackGain(const juce::String& trackName) {
    return audioEngine->getTrackGain(trackName);
}

// --- Bus management ---

void PerformanceAPI::createBus(const juce::String& name) {
    if (!currentSongId.empty()) {
        registry->createBus(currentSongId, name.toStdString());
        engineSync->sync(currentSongId);
    } else {
        audioEngine->createBus(name);
    }
}

void PerformanceAPI::removeBus(const juce::String& name) {
    audioEngine->removeBus(name);
}

void PerformanceAPI::addBusEffect(const juce::String& busName, const juce::String& effectName,
                                   const juce::String& pluginName) {
    if (!currentSongId.empty()) {
        auto plugin = registry->findPluginByName(pluginName.toStdString());
        if (plugin) {
            for (auto& b : registry->bussesForSong(currentSongId)) {
                if (b.name == busName.toStdString()) {
                    registry->createEffect(b.id, EntityType::Bus,
                                           effectName.toStdString(), plugin->id);
                    break;
                }
            }
        }
        engineSync->sync(currentSongId);
    } else {
        audioEngine->addBusEffect(busName, effectName, pluginName);
    }
}

void PerformanceAPI::setBusGain(const juce::String& busName, float gain) {
    audioEngine->setBusGain(busName, gain);
}

// --- Sends ---

void PerformanceAPI::addSend(const juce::String& trackName, const juce::String& busName, float gain) {
    if (!currentSongId.empty()) {
        std::string trackId, busId;
        for (auto& t : registry->tracksForSong(currentSongId)) {
            if (t.name == trackName.toStdString()) { trackId = t.id; break; }
        }
        for (auto& b : registry->bussesForSong(currentSongId)) {
            if (b.name == busName.toStdString()) { busId = b.id; break; }
        }
        if (!trackId.empty() && !busId.empty())
            registry->createSend(trackId, busId, gain);
        engineSync->sync(currentSongId);
    } else {
        audioEngine->addSend(trackName, busName, gain);
    }
}

void PerformanceAPI::setSendGain(const juce::String& trackName, const juce::String& busName, float gain) {
    audioEngine->setSendGain(trackName, busName, gain);
}

// --- Parameters ---

static juce::AudioProcessorParameter* findParamInternal(SongRuntime& runtime,
                                                          const juce::String& trackName,
                                                          const juce::String& effectName,
                                                          const juce::String& paramName) {
    return runtime.findParam(trackName, effectName, paramName);
}

void PerformanceAPI::setParam(const juce::String& trackName, const juce::String& paramName, float value) {
    if (auto* param = findParamInternal(*songRuntime, trackName, "", paramName))
        param->setValueNotifyingHost(value);
}

void PerformanceAPI::setEffectParam(const juce::String& trackName, const juce::String& effectName,
                                     const juce::String& paramName, float value) {
    if (auto* param = findParamInternal(*songRuntime, trackName, effectName, paramName))
        param->setValueNotifyingHost(value);
}

float PerformanceAPI::getParam(const juce::String& trackName, const juce::String& paramName) {
    if (auto* param = findParamInternal(*songRuntime, trackName, "", paramName))
        return param->getValue();
    return 0.0f;
}

float PerformanceAPI::getEffectParam(const juce::String& trackName, const juce::String& effectName,
                                      const juce::String& paramName) {
    if (auto* param = findParamInternal(*songRuntime, trackName, effectName, paramName))
        return param->getValue();
    return 0.0f;
}

// --- MIDI control binding ---

static MIDIControl::Type parseControlType(const juce::String& type) {
    if (type.equalsIgnoreCase("cc")) return MIDIControl::CC;
    if (type.equalsIgnoreCase("note")) return MIDIControl::Note;
    if (type.equalsIgnoreCase("pitchbend")) return MIDIControl::PitchBend;
    if (type.equalsIgnoreCase("pressure")) return MIDIControl::Pressure;
    return MIDIControl::CC;
}

// Resolve arg names to entity IDs using the action's param schema
juce::String PerformanceAPI::resolveArgsToIds(const juce::String& actionName,
                                                const juce::String& argsJson) {
    auto action = registry->findActionByName(actionName.toStdString());
    if (!action || action->paramSchema.empty()) return argsJson;

    auto schema = juce::JSON::parse(juce::String(action->paramSchema));
    auto args = juce::JSON::parse(argsJson);
    auto* schemaArr = schema.getArray();
    auto* argsArr = args.getArray();
    if (!schemaArr || !argsArr) return argsJson;

    juce::Array<juce::var> resolved;
    for (int i = 0; i < argsArr->size() && i < schemaArr->size(); ++i) {
        auto paramType = (*schemaArr)[i].getProperty("type", "").toString();
        auto argVal = (*argsArr)[i].toString();

        if (paramType == "string") {
            // Resolve entity name to ID based on context
            // Check tracks first, then busses, then songs
            bool found = false;
            if (!currentSongId.empty()) {
                for (auto& t : registry->tracksForSong(currentSongId)) {
                    if (t.name == argVal.toStdString()) {
                        resolved.add(juce::var(juce::String(t.id)));
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    for (auto& b : registry->bussesForSong(currentSongId)) {
                        if (b.name == argVal.toStdString()) {
                            resolved.add(juce::var(juce::String(b.id)));
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (!found) {
                auto song = registry->findSongByName(argVal.toStdString());
                if (song) {
                    resolved.add(juce::var(juce::String(song->id)));
                    found = true;
                }
            }
            // If not an entity name, keep as-is (e.g., easing names)
            if (!found)
                resolved.add((*argsArr)[i]);
        } else {
            resolved.add((*argsArr)[i]);
        }
    }

    return juce::JSON::toString(juce::var(resolved));
}

// Resolve entity ID back to name for engine calls
juce::String PerformanceAPI::resolveIdToName(const juce::String& id) {
    auto entity = registry->get(id.toStdString());
    if (entity) return juce::String(entity->get("name"));
    return id;  // not an ID, return as-is
}

void PerformanceAPI::bind(const juce::String& controlType, int channel, int number,
                           const juce::String& actionName, const juce::String& argsJson,
                           const juce::String& description) {
    auto action = registry->findActionByName(actionName.toStdString());
    if (!action) {
        perfLog("[API] Unknown action: %s\n", actionName.toRawUTF8());
        return;
    }

    // Resolve names to IDs for storage
    auto resolvedArgs = resolveArgsToIds(actionName, argsJson);

    // Register runtime handler
    auto actionNameStr = actionName.toStdString();
    auto resolvedArgsStr = resolvedArgs.toStdString();
    MIDIControl control = { parseControlType(controlType), channel, number };
    songRuntime->addBinding(control, [this, actionNameStr, resolvedArgsStr](float value) {
        auto args = juce::JSON::parse(juce::String(resolvedArgsStr));
        executeAction(actionNameStr, args, value);
    }, description);

    // Persist to registry with IDs
    if (!currentSongId.empty()) {
        registry->createBinding(currentSongId, controlType.toStdString(), channel, number,
                                 action->id, resolvedArgs.toStdString(), description.toStdString());
    }
}

void PerformanceAPI::unbind(const juce::String& controlType, int channel, int number) {
    MIDIControl control = { parseControlType(controlType), channel, number };
    songRuntime->removeBinding(control);
}

void PerformanceAPI::unbindAll() {
    songRuntime->clearBindings();
}

// --- Presets ---

std::vector<juce::String> PerformanceAPI::listPresets(const juce::String& trackName) {
    std::vector<juce::String> presets;
    if (auto* proc = audioEngine->getTrackInstrumentProcessor(trackName)) {
        for (int i = 0; i < proc->getNumPrograms(); ++i)
            presets.push_back(proc->getProgramName(i));
    }
    return presets;
}

void PerformanceAPI::loadPreset(const juce::String& trackName, int index) {
    // Send MIDI program change — more widely supported than JUCE's setCurrentProgram
    auto msg = juce::MidiMessage::programChange(1, index);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    audioEngine->injectMidi(msg);
    perfLog("[API] Sent program change %d for track \"%s\"\n", index, trackName.toRawUTF8());
}

void PerformanceAPI::loadPresetByName(const juce::String& trackName, const juce::String& presetName) {
    if (auto* proc = audioEngine->getTrackInstrumentProcessor(trackName)) {
        for (int i = 0; i < proc->getNumPrograms(); ++i) {
            if (proc->getProgramName(i).containsIgnoreCase(presetName)) {
                proc->setCurrentProgram(i);
                perfLog("[API] Loaded preset \"%s\" (%d) on track \"%s\"\n",
                        proc->getProgramName(i).toRawUTF8(), i, trackName.toRawUTF8());
                return;
            }
        }
        perfLog("[API] Preset not found: \"%s\" on track \"%s\"\n",
                presetName.toRawUTF8(), trackName.toRawUTF8());
    }
}

// --- Plugin state snapshots ---

static juce::File getSnapshotsDir() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".config/performance/snapshots");
}

void PerformanceAPI::saveSnapshot(const juce::String& trackName, const juce::String& snapshotName) {
    auto* proc = audioEngine->getTrackInstrumentProcessor(trackName);
    if (!proc) {
        perfLog("[API] Cannot save snapshot: no instrument on track \"%s\"\n", trackName.toRawUTF8());
        return;
    }

    juce::MemoryBlock state;
    proc->getStateInformation(state);

    auto dir = getSnapshotsDir().getChildFile(proc->getName());
    dir.createDirectory();
    auto file = dir.getChildFile(snapshotName + ".state");
    file.replaceWithData(state.getData(), state.getSize());

    // Register in registry
    auto plugin = registry->findPluginByName(proc->getName().toStdString());
    if (plugin) {
        registry->createSnapshot(plugin->id, snapshotName.toStdString(),
                                  file.getFullPathName().toStdString());
    }

    perfLog("[API] Saved snapshot \"%s\" for %s (%d bytes)\n",
            snapshotName.toRawUTF8(), proc->getName().toRawUTF8(), (int)state.getSize());
}

void PerformanceAPI::loadSnapshot(const juce::String& trackName, const juce::String& snapshotName) {
    auto* proc = audioEngine->getTrackInstrumentProcessor(trackName);
    if (!proc) {
        perfLog("[API] Cannot load snapshot: no instrument on track \"%s\"\n", trackName.toRawUTF8());
        return;
    }

    auto file = getSnapshotsDir().getChildFile(proc->getName()).getChildFile(snapshotName + ".state");
    if (!file.existsAsFile()) {
        perfLog("[API] Snapshot not found: \"%s\" for %s\n",
                snapshotName.toRawUTF8(), proc->getName().toRawUTF8());
        return;
    }

    juce::MemoryBlock state;
    file.loadFileAsData(state);
    proc->setStateInformation(state.getData(), (int)state.getSize());

    perfLog("[API] Loaded snapshot \"%s\" for %s\n",
            snapshotName.toRawUTF8(), proc->getName().toRawUTF8());
}

std::vector<juce::String> PerformanceAPI::listSnapshots(const juce::String& pluginName) {
    std::vector<juce::String> names;
    auto dir = getSnapshotsDir().getChildFile(pluginName);
    if (!dir.isDirectory()) return names;

    for (auto& entry : juce::RangedDirectoryIterator(dir, false, "*.state")) {
        names.push_back(entry.getFile().getFileNameWithoutExtension());
    }
    std::sort(names.begin(), names.end());
    return names;
}

// --- Automation ---

int PerformanceAPI::interpolate(float from, float to, float durationSec,
                                 AutomationCallback callback, EasingFn easing) {
    return automationEngine->interpolate(from, to, durationSec, std::move(callback), std::move(easing));
}

int PerformanceAPI::delay(float delaySec, std::function<void()> callback) {
    return automationEngine->delay(delaySec, std::move(callback));
}

void PerformanceAPI::cancelAutomation(int handle) {
    automationEngine->cancel(handle);
}

void PerformanceAPI::cancelAllAutomation() {
    automationEngine->cancelAll();
}

// --- Plugin UI ---

void PerformanceAPI::openPluginEditor(const juce::String& trackName, const juce::String& effectName) {
    audioEngine->openPluginEditor(trackName, effectName);
}

void PerformanceAPI::closeTopPluginEditor() {
    audioEngine->closeTopPluginEditor();
}

// --- Song management ---

std::string PerformanceAPI::createSong(const juce::String& name) {
    // Clear engine state
    engineSync->clear();

    // If song already exists, reuse it but clear its children
    auto existing = registry->findSongByName(name.toStdString());
    if (existing) {
        currentSongId = existing->id;
        registry->deleteSong(currentSongId);
    }
    currentSongId = registry->createSong(name.toStdString());
    perfLog("[API] Created song \"%s\" (id: %s)\n", name.toRawUTF8(), currentSongId.c_str());
    return currentSongId;
}

void PerformanceAPI::loadSong(const SongDef& song) {
    songRuntime->load(song);
}

void PerformanceAPI::loadSongFromRegistry(const std::string& songId) {
    auto song = registry->findSongById(songId);
    if (!song) {
        perfLog("[API] Song not found: %s\n", songId.c_str());
        return;
    }

    unloadSong();
    currentSongId = songId;
    perfLog("[API] Loading song from registry: %s\n", song->name.c_str());

    // Create busses first
    for (auto& bus : registry->bussesForSong(songId)) {
        audioEngine->createBus(juce::String(bus.name));
        audioEngine->setBusGain(juce::String(bus.name), bus.outputGain);
        for (auto& fx : registry->effectsForParent(bus.id)) {
            auto plugin = registry->findPluginById(fx.pluginId);
            if (plugin)
                audioEngine->addBusEffect(juce::String(bus.name), juce::String(fx.name),
                                           juce::String(plugin->name));
        }
    }

    // Create tracks
    for (auto& track : registry->tracksForSong(songId)) {
        auto plugin = registry->findPluginById(track.pluginId);
        if (!plugin) {
            perfLog("[API] Plugin not found for track \"%s\", skipping\n", track.name.c_str());
            continue;
        }

        audioEngine->createTrack(juce::String(track.name));

        // Resolve snapshot
        juce::String snapshotName;
        if (!track.snapshotId.empty()) {
            auto snap = registry->findSnapshotById(track.snapshotId);
            if (snap) snapshotName = juce::String(snap->name);
        }

        audioEngine->addTrackInstrument(juce::String(track.name), juce::String(plugin->name),
            [this, trackName = track.name, snapshotName] {
                if (snapshotName.isNotEmpty())
                    loadSnapshot(juce::String(trackName), snapshotName);
                audioEngine->openPluginEditor(juce::String(trackName));
            });

        for (auto& fx : registry->effectsForParent(track.id)) {
            auto fxPlugin = registry->findPluginById(fx.pluginId);
            if (fxPlugin)
                audioEngine->addTrackEffect(juce::String(track.name), juce::String(fx.name),
                                             juce::String(fxPlugin->name));
        }

        audioEngine->setTrackGain(juce::String(track.name), track.outputGain);
        if (!track.midiEnabled)
            audioEngine->setTrackMidiEnabled(juce::String(track.name), false);

        for (auto& send : registry->sendsForTrack(track.id))  {
            // Find bus name from registry
            for (auto& bus : registry->bussesForSong(songId)) {
                if (bus.id == send.busId) {
                    audioEngine->addSend(juce::String(track.name), juce::String(bus.name), send.gain);
                    break;
                }
            }
        }
    }

    // Load bindings
    for (auto& binding : registry->bindingsForSong(songId)) {
        auto action = registry->findActionById(binding.actionId);
        if (!action) continue;

        // Parse args JSON and dispatch to the appropriate action
        auto argsJson = juce::JSON::parse(juce::String(binding.args));

        // Build a Lua handler that calls the named action with the stored args
        // For now, we'll construct this via the Lua engine when it's wired up
        perfLog("[API] Binding: %s ch%d #%d -> %s\n",
                binding.controlType.c_str(), binding.channel, binding.number,
                action->name.c_str());
    }

    perfLog("[API] Song loaded from registry: %s\n", song->name.c_str());
}

void PerformanceAPI::unloadSong() {
    currentSongId.clear();
    songRuntime->unload();
}

SongDef PerformanceAPI::getCurrentSongDef() const {
    SongDef song;
    song.name = songRuntime->getSongName();

    for (auto& trackName : audioEngine->getTrackNames()) {
        TrackDef t;
        t.name = trackName;
        t.pluginName = audioEngine->getTrackPluginName(trackName);
        t.outputGain = audioEngine->getTrackGain(trackName);
        t.midiEnabled = audioEngine->isTrackMidiEnabled(trackName);

        for (auto& fx : audioEngine->getTrackEffects(trackName))
            t.effects.push_back({ fx.name, fx.pluginName, {} });

        for (auto& send : audioEngine->getTrackSends(trackName))
            t.sends.push_back({ send.busName, send.gain });

        song.tracks.push_back(std::move(t));
    }

    for (auto& busName : audioEngine->getBusNames()) {
        BusDef b;
        b.name = busName;
        b.outputGain = audioEngine->getBusGain(busName);

        for (auto& fx : audioEngine->getBusEffects(busName))
            b.effects.push_back({ fx.name, fx.pluginName, {} });

        song.busses.push_back(std::move(b));
    }

    return song;
}

void PerformanceAPI::saveSongToFile(const juce::String& name) {
    auto song = getCurrentSongDef();
    song.name = name;

    // Auto-save snapshots for each plugin, using the song name as prefix
    for (auto& trackName : audioEngine->getTrackNames()) {
        auto* proc = audioEngine->getTrackInstrumentProcessor(trackName);
        if (!proc) continue;

        auto snapshotName = name + "_" + trackName;
        saveSnapshot(trackName, snapshotName);

        // Find and update the track's snapshot name in the song def
        for (auto& t : song.tracks) {
            if (t.name == trackName)
                t.snapshotName = snapshotName;
        }
    }

    // Save JSON
    auto songsDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                        .getChildFile(".config/performance/songs");
    songsDir.createDirectory();
    auto file = songsDir.getChildFile(name + ".json");
    file.replaceWithText(song.toJson());

    perfLog("[API] Saved song \"%s\" to %s\n", name.toRawUTF8(), file.getFullPathName().toRawUTF8());
}

bool PerformanceAPI::isSongLoaded() const {
    return songRuntime->isLoaded();
}

juce::String PerformanceAPI::getSongName() const {
    return songRuntime->getSongName();
}

// --- Query ---

std::vector<juce::String> PerformanceAPI::listPlugins() const {
    std::vector<juce::String> names;
    for (auto& type : audioEngine->getKnownPlugins().getTypes())
        names.push_back(type.name);
    return names;
}

// --- Generic Registry CRUD ---

std::string PerformanceAPI::registryCreate(const std::string& type,
                                            const std::map<std::string, std::string>& fields) {
    return registry->create(type, fields);
}

std::map<std::string, std::string> PerformanceAPI::registryGet(const std::string& id) {
    auto entity = registry->get(id);
    if (!entity) return {};
    auto result = entity->fields;
    result["_type"] = entity->type;
    return result;
}

std::vector<std::map<std::string, std::string>> PerformanceAPI::registryList(
        const std::string& type, const std::map<std::string, std::string>& filters) {
    std::vector<std::map<std::string, std::string>> result;
    for (auto& entity : registry->list(type, filters)) {
        auto row = entity.fields;
        row["_type"] = entity.type;
        result.push_back(row);
    }
    return result;
}

void PerformanceAPI::registryUpdate(const std::string& id,
                                     const std::map<std::string, std::string>& fields) {
    registry->update(id, fields);
}

void PerformanceAPI::registryDelete(const std::string& id) {
    registry->remove(id);
}

std::vector<juce::String> PerformanceAPI::listTrackNames() const {
    return audioEngine->getTrackNames();
}

juce::String PerformanceAPI::getTrackPluginName(const juce::String& trackName) const {
    return audioEngine->getTrackPluginName(trackName);
}

std::vector<juce::String> PerformanceAPI::getTrackEffectNames(const juce::String& trackName) const {
    std::vector<juce::String> names;
    for (auto& fx : audioEngine->getTrackEffects(trackName))
        names.push_back(fx.name);
    return names;
}

bool PerformanceAPI::isTrackMidiEnabled(const juce::String& trackName) const {
    return audioEngine->isTrackMidiEnabled(trackName);
}

void PerformanceAPI::log(const juce::String& message) {
    perfLog("[API] %s\n", message.toRawUTF8());
}

juce::AudioDeviceManager& PerformanceAPI::getDeviceManager() {
    return audioEngine->getDeviceManager();
}

// --- Registry population ---

void PerformanceAPI::populatePluginRegistry() {
    int count = 0;
    for (auto& type : audioEngine->getKnownPlugins().getTypes()) {
        registry->registerPlugin(
            type.name.toStdString(),
            type.manufacturerName.toStdString(),
            type.fileOrIdentifier.toStdString());
        count++;
    }
    perfLog("[API] Registered %d plugins in registry\n", count);
}

void PerformanceAPI::registerBuiltinActions() {
    registry->registerAction("setSingleActiveTrack", R"([{"name":"trackName","type":"string"}])");
    registry->registerAction("setTrackMidiEnabled", R"([{"name":"trackName","type":"string"},{"name":"enabled","type":"bool"}])");
    registry->registerAction("setTrackGain", R"([{"name":"trackName","type":"string"},{"name":"gain","type":"float"}])");
    registry->registerAction("setBusGain", R"([{"name":"busName","type":"string"},{"name":"gain","type":"float"}])");
    registry->registerAction("setSendGain", R"([{"name":"trackName","type":"string"},{"name":"busName","type":"string"},{"name":"gain","type":"float"}])");
    registry->registerAction("fadeOut", R"([{"name":"trackName","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])");
    registry->registerAction("fadeIn", R"([{"name":"trackName","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])");
    registry->registerAction("crossfade", R"([{"name":"fromTrack","type":"string"},{"name":"toTrack","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])");
    registry->registerAction("loadSong", R"([{"name":"songName","type":"string"}])");
    registry->registerAction("openEditor", R"([{"name":"trackName","type":"string"}])");
    registry->registerAction("log", R"([{"name":"message","type":"string"}])");

    perfLog("[API] Registered %d built-in actions\n", (int)registry->allActions().size());
}

void PerformanceAPI::executeAction(const std::string& actionName, const juce::var& args, float value) {
    // Guard: most actions should ignore note-off / CC release
    if (value == 0.0f) return;

    auto getArg = [&](int index) -> juce::String {
        if (auto* arr = args.getArray())
            if (index < arr->size())
                return resolveIdToName((*arr)[index].toString());
        return {};
    };
    auto getArgFloat = [&](int index, float def = 0.0f) -> float {
        if (auto* arr = args.getArray())
            if (index < arr->size())
                return (float)(*arr)[index];
        return def;
    };

    if (actionName == "setSingleActiveTrack") {
        auto trackName = getArg(0);
        for (auto& name : audioEngine->getTrackNames())
            audioEngine->setTrackMidiEnabled(name, name == trackName);
        perfLog("[Action] setSingleActiveTrack: %s\n", trackName.toRawUTF8());
    }
    else if (actionName == "setTrackMidiEnabled") {
        audioEngine->setTrackMidiEnabled(getArg(0), getArg(1) == "true");
    }
    else if (actionName == "setTrackGain") {
        audioEngine->setTrackGain(getArg(0), getArgFloat(1));
    }
    else if (actionName == "setBusGain") {
        audioEngine->setBusGain(getArg(0), getArgFloat(1));
    }
    else if (actionName == "fadeOut") {
        auto track = getArg(0).toStdString();
        auto dur = getArgFloat(1, 3.0f);
        auto easing = getArg(2).toStdString();
        auto easingFn = AutomationEngine::easingByName(easing.empty() ? "cosine" : easing);
        float current = audioEngine->getTrackGain(juce::String(track));
        automationEngine->interpolate(current, 0.0f, dur,
            [this, track](float v) { audioEngine->setTrackGain(juce::String(track), v); },
            easingFn);
    }
    else if (actionName == "fadeIn") {
        auto track = getArg(0).toStdString();
        auto dur = getArgFloat(1, 3.0f);
        auto easing = getArg(2).toStdString();
        auto easingFn = AutomationEngine::easingByName(easing.empty() ? "cosine" : easing);
        float current = audioEngine->getTrackGain(juce::String(track));
        automationEngine->interpolate(current, 1.0f, dur,
            [this, track](float v) { audioEngine->setTrackGain(juce::String(track), v); },
            easingFn);
    }
    else if (actionName == "crossfade") {
        auto from = getArg(0).toStdString();
        auto to = getArg(1).toStdString();
        auto dur = getArgFloat(2, 3.0f);
        auto easing = getArg(3).toStdString();
        auto easingFn = AutomationEngine::easingByName(easing.empty() ? "cosine" : easing);
        automationEngine->interpolate(1.0f, 0.0f, dur,
            [this, from](float v) { audioEngine->setTrackGain(juce::String(from), v); }, easingFn);
        automationEngine->interpolate(0.0f, 1.0f, dur,
            [this, to](float v) { audioEngine->setTrackGain(juce::String(to), v); }, easingFn);
    }
    else if (actionName == "loadSong") {
        // Handled via Lua loadSong — defer
        perfLog("[Action] loadSong not yet implemented via action dispatch\n");
    }
    else if (actionName == "openEditor") {
        audioEngine->openPluginEditor(getArg(0));
    }
    else if (actionName == "log") {
        perfLog("[Action] %s\n", getArg(0).toRawUTF8());
    }
    else {
        perfLog("[Action] Unknown action: %s\n", actionName.c_str());
    }
}
