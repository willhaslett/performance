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
    if (!currentSongId.empty()) {
        registry->createTrack(currentSongId, name.toStdString(), "");
        engineSync->sync(currentSongId);
    } else {
        audioEngine->createTrack(name);
    }
}

void PerformanceAPI::renameTrack(const juce::String& oldName, const juce::String& newName) {
    audioEngine->renameTrack(oldName, newName);
    engineSync->notifyRenamed(oldName.toStdString(), newName.toStdString());
    if (!currentSongId.empty()) {
        for (auto& t : registry->tracksForSong(currentSongId)) {
            if (t.name == oldName.toStdString()) {
                registry->update(t.id, {{"name", newName.toStdString()}});
                break;
            }
        }
    }
}

void PerformanceAPI::removeTrack(const juce::String& name) {
    audioEngine->removeTrack(name);

    if (!currentSongId.empty()) {
        for (auto& t : registry->tracksForSong(currentSongId)) {
            if (t.name == name.toStdString()) {
                registry->remove(t.id);  // CASCADE deletes effects, sends, etc.
                break;
            }
        }
        engineSync->notifyRemoved(name.toStdString());
    }
}

void PerformanceAPI::addInstrument(const juce::String& trackName, const juce::String& pluginName,
                                    const juce::String& presetName) {
    auto plugin = registry->findPluginByName(pluginName.toStdString());
    if (!plugin) {
        perfLog("[API] Plugin not found: %s\n", pluginName.toRawUTF8());
        return;
    }

    std::string presetId;
    if (presetName.isNotEmpty()) {
        auto snap = registry->findPreset(plugin->id, presetName.toStdString());
        if (snap) presetId = snap->id;
    }

    if (!currentSongId.empty()) {
        // Check if track already exists in registry (e.g., created empty)
        bool updated = false;
        for (auto& t : registry->tracksForSong(currentSongId)) {
            if (t.name == trackName.toStdString()) {
                // Update existing track with plugin
                t.pluginId = plugin->id;
                t.presetId = presetId;
                registry->updateTrack(t);
                updated = true;
                break;
            }
        }
        if (!updated)
            registry->createTrack(currentSongId, trackName.toStdString(), plugin->id, presetId);

        // Engine already has the track — just load the instrument
        audioEngine->addTrackInstrument(trackName, pluginName, nullptr);
    } else {
        audioEngine->addTrackInstrument(trackName, pluginName, nullptr);
    }
}

void PerformanceAPI::removeInstrument(const juce::String& trackName) {
    audioEngine->removeTrackInstrument(trackName);

    if (!currentSongId.empty()) {
        for (auto& t : registry->tracksForSong(currentSongId)) {
            if (t.name == trackName.toStdString()) {
                t.pluginId = "";
                t.presetId = "";
                registry->updateTrack(t);
                break;
            }
        }
    }
}

void PerformanceAPI::addEffect(const juce::String& parentName, const juce::String& effectName,
                                const juce::String& pluginName) {
    // Master output — direct to engine, no registry entity
    if (parentName == "Output") {
        audioEngine->addEffect(parentName, effectName, pluginName);
        return;
    }

    if (!currentSongId.empty()) {
        auto plugin = registry->findPluginByName(pluginName.toStdString());
        if (plugin) {
            // Check tracks first, then busses
            for (auto& t : registry->tracksForSong(currentSongId)) {
                if (t.name == parentName.toStdString()) {
                    registry->createEffect(t.id, EntityType::Track,
                                           effectName.toStdString(), plugin->id);
                    engineSync->sync(currentSongId);
                    return;
                }
            }
            for (auto& b : registry->bussesForSong(currentSongId)) {
                if (b.name == parentName.toStdString()) {
                    registry->createEffect(b.id, EntityType::Bus,
                                           effectName.toStdString(), plugin->id);
                    engineSync->sync(currentSongId);
                    return;
                }
            }
        }
    } else {
        audioEngine->addEffect(parentName, effectName, pluginName);
    }
}

void PerformanceAPI::removeEffect(const juce::String& parentName, const juce::String& effectName) {
    audioEngine->removeEffect(parentName, effectName);

    if (parentName == "Output") return;  // no registry entity for master output

    if (!currentSongId.empty()) {
        // Check tracks first, then busses
        for (auto& t : registry->tracksForSong(currentSongId)) {
            if (t.name == parentName.toStdString()) {
                for (auto& fx : registry->effectsForParent(t.id)) {
                    if (fx.name == effectName.toStdString()) {
                        registry->remove(fx.id);
                        return;
                    }
                }
                return;
            }
        }
        for (auto& b : registry->bussesForSong(currentSongId)) {
            if (b.name == parentName.toStdString()) {
                for (auto& fx : registry->effectsForParent(b.id)) {
                    if (fx.name == effectName.toStdString()) {
                        registry->remove(fx.id);
                        return;
                    }
                }
                return;
            }
        }
    }
}

void PerformanceAPI::setTrackMidiEnabled(const juce::String& trackName, bool enabled) {
    audioEngine->setTrackMidiEnabled(trackName, enabled);

    // Persist discrete state change immediately
    if (!currentSongId.empty()) {
        for (auto& t : registry->tracksForSong(currentSongId)) {
            if (t.name == trackName.toStdString()) {
                t.midiEnabled = enabled;
                registry->updateTrack(t);
                break;
            }
        }
    }
}

void PerformanceAPI::setTrackGain(const juce::String& trackName, float gain) {
    audioEngine->setTrackGain(trackName, gain);
}

float PerformanceAPI::getTrackGain(const juce::String& trackName) {
    return audioEngine->getTrackGain(trackName);
}

float PerformanceAPI::getTrackPeakLevel(const juce::String& trackName) {
    return audioEngine->getTrackPeakLevel(trackName);
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

void PerformanceAPI::renameBus(const juce::String& oldName, const juce::String& newName) {
    audioEngine->renameBus(oldName, newName);
    engineSync->notifyRenamed(oldName.toStdString(), newName.toStdString());
    if (!currentSongId.empty()) {
        for (auto& b : registry->bussesForSong(currentSongId)) {
            if (b.name == oldName.toStdString()) {
                registry->update(b.id, {{"name", newName.toStdString()}});
                break;
            }
        }
    }
}

void PerformanceAPI::removeBus(const juce::String& name) {
    audioEngine->removeBus(name);

    if (!currentSongId.empty()) {
        for (auto& b : registry->bussesForSong(currentSongId)) {
            if (b.name == name.toStdString()) {
                registry->remove(b.id);  // CASCADE deletes effects
                break;
            }
        }
        engineSync->notifyRemoved(name.toStdString());
    }
}


// --- Master output ---

void PerformanceAPI::setMasterGain(float gain) {
    audioEngine->setMasterGain(gain);
}

float PerformanceAPI::getMasterGain() {
    return audioEngine->getMasterGain();
}

float PerformanceAPI::getMasterPeakLevel() {
    return audioEngine->getMasterPeakLevel();
}

std::vector<juce::String> PerformanceAPI::getMasterEffectNames() {
    std::vector<juce::String> names;
    for (auto& fx : audioEngine->getMasterEffects())
        names.push_back(fx.pluginName);
    return names;
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

// AU program presets removed — plugin state presets are the canonical preset system

// --- Plugin state presets ---

static juce::File getPresetsDir() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".config/performance/snapshots");
}

void PerformanceAPI::savePreset(const juce::String& trackName, const juce::String& presetName) {
    auto* proc = audioEngine->getTrackInstrumentProcessor(trackName);
    if (!proc) {
        perfLog("[API] Cannot save preset: no instrument on track \"%s\"\n", trackName.toRawUTF8());
        return;
    }

    juce::MemoryBlock state;
    proc->getStateInformation(state);

    auto dir = getPresetsDir().getChildFile(proc->getName());
    dir.createDirectory();
    auto file = dir.getChildFile(presetName + ".state");
    file.replaceWithData(state.getData(), state.getSize());

    // Register in registry
    auto plugin = registry->findPluginByName(proc->getName().toStdString());
    if (plugin) {
        registry->createPreset(plugin->id, presetName.toStdString(),
                                  file.getFullPathName().toStdString());
    }

    perfLog("[API] Saved preset \"%s\" for %s (%d bytes)\n",
            presetName.toRawUTF8(), proc->getName().toRawUTF8(), (int)state.getSize());
}

void PerformanceAPI::loadPreset(const juce::String& trackName, const juce::String& presetName) {
    auto* proc = audioEngine->getTrackInstrumentProcessor(trackName);
    if (!proc) {
        perfLog("[API] Cannot load preset: no instrument on track \"%s\"\n", trackName.toRawUTF8());
        return;
    }

    auto file = getPresetsDir().getChildFile(proc->getName()).getChildFile(presetName + ".state");
    if (!file.existsAsFile()) {
        perfLog("[API] Preset not found: \"%s\" for %s\n",
                presetName.toRawUTF8(), proc->getName().toRawUTF8());
        return;
    }

    juce::MemoryBlock state;
    file.loadFileAsData(state);
    proc->setStateInformation(state.getData(), (int)state.getSize());

    perfLog("[API] Loaded preset \"%s\" for %s\n",
            presetName.toRawUTF8(), proc->getName().toRawUTF8());
}

std::vector<juce::String> PerformanceAPI::listPresets(const juce::String& pluginName) {
    std::vector<juce::String> names;
    auto dir = getPresetsDir().getChildFile(pluginName);
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
    engineSync->setActiveSong(currentSongId);
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
    engineSync->clear();
    currentSongId = songId;
    engineSync->setActiveSong(currentSongId);
    perfLog("[API] Loading song from registry: %s\n", song->name.c_str());

    // Let EngineSync build the engine from registry
    engineSync->sync(currentSongId);

    // Restore master gain
    float masterGain = registry->getMasterGain(currentSongId);
    audioEngine->setMasterGain(masterGain);

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

bool PerformanceAPI::restoreSession() {
    auto songs = registry->allSongs();

    if (songs.empty()) {
        // First run — create a default unnamed session
        currentSongId = registry->createSong("Sandbox");
        engineSync->setActiveSong(currentSongId);
        perfLog("[API] Created default session\n");
        return true;
    }

    // Restore existing session
    auto& song = songs[0];
    currentSongId = song.id;
    engineSync->setActiveSong(currentSongId);
    perfLog("[API] Restoring session: %s\n", song.name.c_str());

    // Sync engine from registry
    engineSync->clear();
    engineSync->sync(currentSongId);

    // Restore bindings
    songRuntime->clearBindings();
    for (auto& binding : registry->bindingsForSong(currentSongId)) {
        auto action = registry->findActionById(binding.actionId);
        if (!action) continue;

        auto actionNameStr = action->name;
        auto argsStr = binding.args;
        MIDIControl control = { parseControlType(juce::String(binding.controlType)),
                                binding.channel, binding.number };

        songRuntime->addBinding(control, [this, actionNameStr, argsStr](float value) {
            auto args = juce::JSON::parse(juce::String(argsStr));
            executeAction(actionNameStr, args, value);
        }, juce::String(binding.description));

        perfLog("[API] Restored binding: %s ch%d #%d -> %s\n",
                binding.controlType.c_str(), binding.channel, binding.number,
                action->name.c_str());
    }

    // Restore master gain
    float masterGain = registry->getMasterGain(currentSongId);
    audioEngine->setMasterGain(masterGain);

    perfLog("[API] Session restored: %s (%d tracks, %d bindings)\n",
            song.name.c_str(),
            (int)registry->tracksForSong(currentSongId).size(),
            (int)registry->bindingsForSong(currentSongId).size());
    return true;
}

void PerformanceAPI::unloadSong() {
    engineSync->setActiveSong("");
    currentSongId.clear();
    songRuntime->unload();
}

SongDef PerformanceAPI::getCurrentSongDef() const {
    SongDef song;
    if (!currentSongId.empty()) {
        auto regSong = registry->findSongById(currentSongId);
        if (regSong) song.name = juce::String(regSong->name);
    }
    if (song.name.isEmpty())
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

void PerformanceAPI::saveInitialState() {
    if (currentSongId.empty()) return;

    // Capture current state as JSON
    auto song = getCurrentSongDef();

    // Capture bindings
    juce::Array<juce::var> bindingsArr;
    for (auto& binding : registry->bindingsForSong(currentSongId)) {
        auto action = registry->findActionById(binding.actionId);
        auto* obj = new juce::DynamicObject();
        obj->setProperty("controlType", juce::String(binding.controlType));
        obj->setProperty("channel", binding.channel);
        obj->setProperty("number", binding.number);
        obj->setProperty("action", action ? juce::String(action->name) : juce::String());
        obj->setProperty("args", juce::String(binding.args));
        obj->setProperty("description", juce::String(binding.description));
        bindingsArr.add(juce::var(obj));
    }

    // Build the full initial state
    auto stateJson = juce::JSON::parse(song.toJson());
    if (auto* obj = stateJson.getDynamicObject())
        obj->setProperty("bindings", bindingsArr);

    auto initialStateStr = juce::JSON::toString(stateJson);

    // Write to songs table
    registry->update(currentSongId, {{"initial_state", initialStateStr.toStdString()}});

    perfLog("[API] Saved initial state for song \"%s\"\n", song.name.toRawUTF8());
}

void PerformanceAPI::loadInitialState() {
    if (currentSongId.empty()) return;

    // Read initial_state from songs table
    auto entity = registry->get(currentSongId);
    if (!entity) return;

    auto initialStateStr = entity->get("initial_state");
    if (initialStateStr.empty()) {
        perfLog("[API] No initial state saved for this song\n");
        return;
    }

    auto songDef = SongDef::fromJson(juce::String(initialStateStr));

    // Use the song's registry name if the saved state has no name
    if (songDef.name.isEmpty()) {
        auto song = registry->findSongById(currentSongId);
        songDef.name = song ? juce::String(song->name) : "Sandbox";
    }

    perfLog("[API] Loading initial state for song \"%s\"\n", songDef.name.toRawUTF8());

    // Preserve score before delete
    auto scoreStr = entity->get("score");

    // Clear engine
    engineSync->clear();

    // Clear live registry state for this song (tracks, busses, etc.)
    registry->deleteSong(currentSongId);
    currentSongId = registry->createSong(songDef.name.toStdString());
    engineSync->setActiveSong(currentSongId);

    // Restore initial_state and score (lost in the delete/recreate)
    std::map<std::string, std::string> songFields = {{"initial_state", initialStateStr}};
    if (!scoreStr.empty())
        songFields["score"] = scoreStr;
    registry->update(currentSongId, songFields);

    // Rebuild from SongDef — create tracks, busses, effects, sends
    for (auto& busDef : songDef.busses) {
        registry->createBus(currentSongId, busDef.name.toStdString(), busDef.outputGain);
        // Bus effects
        for (auto& fx : busDef.effects) {
            auto plugin = registry->findPluginByName(fx.pluginName.toStdString());
            if (plugin) {
                for (auto& b : registry->bussesForSong(currentSongId)) {
                    if (b.name == busDef.name.toStdString()) {
                        registry->createEffect(b.id, EntityType::Bus,
                                               fx.name.toStdString(), plugin->id);
                        break;
                    }
                }
            }
        }
    }

    for (auto& trackDef : songDef.tracks) {
        auto plugin = registry->findPluginByName(trackDef.pluginName.toStdString());
        if (!plugin) continue;

        std::string presetId;
        if (trackDef.presetName.isNotEmpty()) {
            auto snap = registry->findPreset(plugin->id, trackDef.presetName.toStdString());
            if (snap) presetId = snap->id;
        }

        registry->createTrack(currentSongId, trackDef.name.toStdString(),
                               plugin->id, presetId, trackDef.outputGain, trackDef.midiEnabled);

        // Track effects
        for (auto& fx : trackDef.effects) {
            auto fxPlugin = registry->findPluginByName(fx.pluginName.toStdString());
            if (fxPlugin) {
                for (auto& t : registry->tracksForSong(currentSongId)) {
                    if (t.name == trackDef.name.toStdString()) {
                        registry->createEffect(t.id, EntityType::Track,
                                               fx.name.toStdString(), fxPlugin->id);
                        break;
                    }
                }
            }
        }

        // Sends
        for (auto& sendDef : trackDef.sends) {
            std::string trackId, busId;
            for (auto& t : registry->tracksForSong(currentSongId))
                if (t.name == trackDef.name.toStdString()) { trackId = t.id; break; }
            for (auto& b : registry->bussesForSong(currentSongId))
                if (b.name == sendDef.busName.toStdString()) { busId = b.id; break; }
            if (!trackId.empty() && !busId.empty())
                registry->createSend(trackId, busId, sendDef.gain);
        }
    }

    // Sync engine
    engineSync->sync(currentSongId);

    // Restore bindings from initial state
    songRuntime->clearBindings();
    auto parsed = juce::JSON::parse(juce::String(initialStateStr));
    if (auto* bindingsArr = parsed.getProperty("bindings", {}).getArray()) {
        for (auto& b : *bindingsArr) {
            auto controlType = b.getProperty("controlType", "").toString();
            int channel = (int)b.getProperty("channel", 0);
            int number = (int)b.getProperty("number", 0);
            auto actionName = b.getProperty("action", "").toString();
            auto args = b.getProperty("args", "[]").toString();
            auto description = b.getProperty("description", "").toString();

            bind(controlType, channel, number, actionName, args, description);
        }
    }

    perfLog("[API] Initial state loaded\n");
}

void PerformanceAPI::saveScore(const juce::String& scoreJson) {
    if (currentSongId.empty()) return;
    registry->update(currentSongId, {{"score", scoreJson.toStdString()}});
    perfLog("[API] Score saved\n");
}

juce::String PerformanceAPI::getScore() const {
    if (currentSongId.empty()) return "[]";
    auto entity = registry->get(currentSongId);
    if (!entity) return "[]";
    auto score = entity->get("score");
    return score.empty() ? juce::String("[]") : juce::String(score);
}

void PerformanceAPI::replayScore(int upToStep) {
    if (currentSongId.empty()) return;

    // Load initial state first
    loadInitialState();

    // Parse score
    auto scoreJson = getScore();
    auto score = juce::JSON::parse(scoreJson);
    auto* scoreArr = score.getArray();
    if (!scoreArr || scoreArr->isEmpty()) {
        perfLog("[API] No score to replay\n");
        return;
    }

    int steps = (upToStep < 0) ? scoreArr->size() : std::min(upToStep, (int)scoreArr->size());
    perfLog("[API] Replaying score: %d of %d steps\n", steps, (int)scoreArr->size());

    for (int i = 0; i < steps; ++i) {
        auto step = (*scoreArr)[i];
        auto actionName = step.getProperty("action", "").toString().toStdString();
        auto args = step.getProperty("args", juce::var());

        if (actionName.empty()) continue;

        perfLog("[API] Score step %d: %s\n", i + 1, actionName.c_str());
        executeAction(actionName, args, 1.0f);  // 1.0 = trigger value
    }

    perfLog("[API] Score replay complete\n");
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

std::vector<juce::String> PerformanceAPI::listInstrumentPlugins() const {
    std::vector<juce::String> names;
    for (auto& type : audioEngine->getKnownPlugins().getTypes())
        if (type.isInstrument)
            names.push_back(type.name);
    return names;
}

std::vector<juce::String> PerformanceAPI::listEffectPlugins() const {
    std::vector<juce::String> names;
    for (auto& type : audioEngine->getKnownPlugins().getTypes())
        if (!type.isInstrument)
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
    // Protect the default session from deletion
    auto song = registry->findSongById(id);
    if (song && song->name == "Sandbox") return;

    // If deleting the current song, clear engine and fall back
    if (id == currentSongId) {
        engineSync->clear();
        registry->remove(id);
        auto songs = registry->allSongs();
        if (!songs.empty()) {
            loadSongFromRegistry(songs[0].id);
        } else {
            currentSongId = registry->createSong("Sandbox");
            engineSync->setActiveSong(currentSongId);
        }
        return;
    }

    registry->remove(id);
}

std::vector<juce::String> PerformanceAPI::listTrackNames() const {
    return audioEngine->getTrackNames();
}

std::vector<juce::String> PerformanceAPI::listBusNames() const {
    return audioEngine->getBusNames();
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

std::vector<juce::String> PerformanceAPI::getBusEffectNames(const juce::String& busName) const {
    std::vector<juce::String> names;
    for (auto& fx : audioEngine->getBusEffects(busName))
        names.push_back(fx.name);
    return names;
}

float PerformanceAPI::getBusGain(const juce::String& busName) {
    return audioEngine->getBusGain(busName);
}

float PerformanceAPI::getBusPeakLevel(const juce::String& busName) {
    return audioEngine->getBusPeakLevel(busName);
}

std::vector<PerformanceAPI::TrackSendInfo> PerformanceAPI::getTrackSends(const juce::String& trackName) const {
    std::vector<TrackSendInfo> result;
    for (auto& send : audioEngine->getTrackSends(trackName))
        result.push_back({ send.busName, send.gain, send.peakLevel });
    return result;
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
    // Performance actions — things you'd bind to MIDI controls
    registry->registerAction("setActiveTrack", "Set active track",
        R"([{"name":"trackName","type":"string"}])");
    registry->registerAction("enableTrack", "Enable track",
        R"([{"name":"trackName","type":"string"}])");
    registry->registerAction("disableTrack", "Disable track",
        R"([{"name":"trackName","type":"string"}])");
    registry->registerAction("fadeOut", "Fade out",
        R"([{"name":"trackName","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])");
    registry->registerAction("fadeIn", "Fade in",
        R"([{"name":"trackName","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])");
    registry->registerAction("crossfade", "Crossfade",
        R"([{"name":"fromTrack","type":"string"},{"name":"toTrack","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])");

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

    if (actionName == "setActiveTrack") {
        auto trackName = getArg(0);
        for (auto& name : audioEngine->getTrackNames())
            audioEngine->setTrackMidiEnabled(name, name == trackName);
        perfLog("[Action] setActiveTrack: %s\n", trackName.toRawUTF8());
    }
    else if (actionName == "enableTrack") {
        audioEngine->setTrackMidiEnabled(getArg(0), true);
        perfLog("[Action] enableTrack: %s\n", getArg(0).toRawUTF8());
    }
    else if (actionName == "disableTrack") {
        audioEngine->setTrackMidiEnabled(getArg(0), false);
        perfLog("[Action] disableTrack: %s\n", getArg(0).toRawUTF8());
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
    else {
        perfLog("[Action] Unknown action: %s\n", actionName.c_str());
    }
}
