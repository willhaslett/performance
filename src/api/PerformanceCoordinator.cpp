#include "api/PerformanceCoordinator.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "persistence/PersistenceLayer.h"
#include "automation/AutomationEngine.h"
#include "engine/AudioEngine.h"
#include "engine/EngineSync.h"
#include "engine/MIDIEngine.h"
#include "engine/Log.h"
#include "song/SongRuntime.h"
#include <juce_cryptography/juce_cryptography.h>

PerformanceCoordinator::PerformanceCoordinator() {}

PerformanceCoordinator::~PerformanceCoordinator() {
    shutdown();
}

void PerformanceCoordinator::initialise(const juce::String& dbPath) {
    // State store (in-memory)
    stateAPI = std::make_unique<StateAPI>();

    // Persistence (SQLite)
    persistence = std::make_unique<PersistenceLayer>();
    if (dbPath.isNotEmpty()) {
        persistence->open(dbPath.toStdString());
    } else {
        auto configDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                             .getChildFile(".config/performance");
        configDir.createDirectory();
        persistence->open(configDir.getChildFile("state.db").getFullPathName().toStdString());
    }

    // Audio engine
    audioEngine = std::make_unique<AudioEngine>();
    audioEngine->initialise();
    perfLog("[Coordinator] AudioEngine initialised\n");

    // Engine API
    engineAPI = std::make_unique<EngineAPI>(*audioEngine, *stateAPI);

    // Engine sync — must exist before any state mutations so it sees all events
    engineSync = std::make_unique<EngineSync>(*audioEngine, *stateAPI);

    // Load persisted state — establishes plugin/action IDs from DB
    // EngineSync reacts to creation events, building the engine graph
    persistence->loadInto(*stateAPI);

    // Restore saved audio devices (must be after loadInto so config is available)
    {
        auto& dm = audioEngine->getDeviceManager();
        auto setup = dm.getAudioDeviceSetup();
        bool changed = false;

        auto savedOutput = stateAPI->getConfig("audio_output_device");
        auto savedInput = stateAPI->getConfig("audio_input_device");

        perfLog("[Coordinator] Audio config: output saved='%s' current='%s', input saved='%s' current='%s'\n",
                savedOutput.c_str(), setup.outputDeviceName.toRawUTF8(),
                savedInput.c_str(), setup.inputDeviceName.toRawUTF8());

        if (!savedOutput.empty() && setup.outputDeviceName != juce::String(savedOutput)) {
            setup.outputDeviceName = juce::String(savedOutput);
            changed = true;
        }
        if (!savedInput.empty() && setup.inputDeviceName != juce::String(savedInput)) {
            setup.inputDeviceName = juce::String(savedInput);
            changed = true;
        }

        if (changed) {
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty())
                perfLog("[Coordinator] Restored audio devices: out='%s', in='%s'\n",
                        setup.outputDeviceName.toRawUTF8(), setup.inputDeviceName.toRawUTF8());
            else
                perfLog("[Coordinator] Failed to restore audio devices: %s\n", err.toRawUTF8());
        }
    }

    // Then populate from engine scan — deduplicates by name, keeps DB IDs
    populatePluginCatalog();

    // Register built-in actions — deduplicates by name, keeps DB IDs
    registerBuiltinActions();

    automationEngine = std::make_unique<AutomationEngine>();
    songRuntime = std::make_unique<SongRuntime>();

    midiEngine = std::make_unique<MIDIEngine>(
        audioEngine->getDeviceManager(), *audioEngine, *stateAPI);
    midiEngine->setSongRuntime(songRuntime.get());
    midiEngine->setMonitorMode(true);
    midiEngine->initialise();
    perfLog("[Coordinator] MIDIEngine initialised\n");

    // Subscribe to state events for auto-creating Default presets
    stateSubscriptionId = stateAPI->events().subscribe([this](const StateEvent& event) {
        onStateEvent(event);
    });

    // Auto-save every 30 seconds if dirty
    startTimer(30000);
}

void PerformanceCoordinator::timerCallback() {
    if (stateAPI && persistence && stateAPI->isDirty()) {
        persistence->saveFrom(*stateAPI);
        stateAPI->clearDirty();
        perfLog("[Coordinator] Auto-saved\n");
    }
}

void PerformanceCoordinator::shutdown() {
    stopTimer();
    if (stateAPI && stateSubscriptionId >= 0)
        stateAPI->events().unsubscribe(stateSubscriptionId);
    // Full save on shutdown — captures processor state and flushes
    if (stateAPI && persistence) {
        captureProcessorState();
        persistence->saveFrom(*stateAPI);
    }
    songRuntime.reset();
    midiEngine.reset();
    engineSync.reset();
    engineAPI.reset();
    audioEngine.reset();
    automationEngine.reset();
    persistence.reset();
    stateAPI.reset();
}

StateAPI& PerformanceCoordinator::state() { return *stateAPI; }
EngineAPI& PerformanceCoordinator::engine() { return *engineAPI; }

// --- Song lifecycle ---

std::string PerformanceCoordinator::createSong(const juce::String& name) {
    auto songId = stateAPI->createSong(name.toStdString());
    stateAPI->setCurrentSong(songId);
    perfLog("[Coordinator] Created song \"%s\" (id: %s)\n", name.toRawUTF8(), songId.c_str());
    return songId;
}

void PerformanceCoordinator::loadSong(const std::string& songId) {
    auto* song = stateAPI->findSong(songId);
    if (!song) {
        perfLog("[Coordinator] Song not found: %s\n", songId.c_str());
        return;
    }

    // Capture processor state from current song before switching
    captureProcessorState();

    songRuntime->clearBindings();
    stateAPI->setCurrentSong(songId);  // triggers EngineSync via config event
    restoreBindings();

    perfLog("[Coordinator] Loaded song: %s\n", song->name.c_str());
}

bool PerformanceCoordinator::restoreSession() {
    auto& songs = stateAPI->allSongs();

    if (songs.empty()) {
        auto songId = stateAPI->createSong("Sandbox");
        stateAPI->setCurrentSong(songId);
        perfLog("[Coordinator] Created default session\n");
        return true;
    }

    // Restore the last active song from config, or first song
    auto lastSongId = stateAPI->getConfig("current_song_id");
    if (lastSongId.empty() || !stateAPI->findSong(lastSongId))
        lastSongId = songs[0].id;

    stateAPI->setCurrentSong(lastSongId);
    restoreBindings();

    auto* song = stateAPI->currentSong();
    perfLog("[Coordinator] Session restored: %s (%d tracks)\n",
            song ? song->name.c_str() : "?",
            (int)stateAPI->listTracks().size());
    return true;
}

void PerformanceCoordinator::unloadSong() {
    stateAPI->setCurrentSong("");
    songRuntime->clearBindings();
}

// --- Persistence ---

static std::string computeHash(const juce::MemoryBlock& data) {
    juce::SHA256 sha(data);
    return sha.toHexString().toStdString();
}

static void captureProcessorBlob(juce::AudioProcessor* proc, std::string& outState, std::string& outHash) {
    if (!proc) return;
    juce::MemoryBlock block;
    proc->getStateInformation(block);
    if (block.getSize() > 0) {
        outState = block.toBase64Encoding().toStdString();
        outHash = computeHash(block);
    }
}

void PerformanceCoordinator::captureProcessorState() {
    auto* song = stateAPI->currentSong();
    if (!song) return;

    int captured = 0;
    for (auto& track : song->tracks) {
        // Instrument
        auto* proc = audioEngine->getTrackInstrumentProcessor(juce::String(track.id));
        if (proc) {
            std::string newHash;
            std::string newState;
            captureProcessorBlob(proc, newState, newHash);
            if (newHash != track.processorStateHash) {
                track.processorState = std::move(newState);
                track.processorStateHash = std::move(newHash);
                captured++;
            }
        }

        // Effects
        for (auto& fx : track.effects) {
            auto* fxProc = audioEngine->getEffectProcessor(juce::String(track.id), juce::String(fx.id));
            if (fxProc) {
                std::string newHash;
                std::string newState;
                captureProcessorBlob(fxProc, newState, newHash);
                if (newHash != fx.processorStateHash) {
                    fx.processorState = std::move(newState);
                    fx.processorStateHash = std::move(newHash);
                    captured++;
                }
            }
        }
    }

    // Bus effects
    for (auto& bus : song->busses) {
        for (auto& fx : bus.effects) {
            auto* fxProc = audioEngine->getEffectProcessor(juce::String(bus.id), juce::String(fx.id));
            if (fxProc) {
                std::string newHash;
                std::string newState;
                captureProcessorBlob(fxProc, newState, newHash);
                if (newHash != fx.processorStateHash) {
                    fx.processorState = std::move(newState);
                    fx.processorStateHash = std::move(newHash);
                    captured++;
                }
            }
        }
    }

    // Master effects
    for (auto& fx : song->masterEffects) {
        auto* fxProc = audioEngine->getEffectProcessor(juce::String("Output"), juce::String(fx.id));
        if (fxProc) {
            std::string newHash;
            std::string newState;
            captureProcessorBlob(fxProc, newState, newHash);
            if (newHash != fx.processorStateHash) {
                fx.processorState = std::move(newState);
                fx.processorStateHash = std::move(newHash);
                captured++;
            }
        }
    }

    if (captured > 0) {
        stateAPI->markDirty();
        perfLog("[Coordinator] Captured %d processor states\n", captured);
    }
}

void PerformanceCoordinator::save() {
    captureProcessorState();
    if (persistence && stateAPI) {
        persistence->saveFrom(*stateAPI);
        perfLog("[Coordinator] Saved\n");
    }
}

// --- Song state snapshots ---

void PerformanceCoordinator::saveInitialState() {
    // TODO: serialize current state to SongState.initialState JSON
    perfLog("[Coordinator] saveInitialState not yet implemented for new system\n");
}

void PerformanceCoordinator::loadInitialState() {
    // TODO: deserialize SongState.initialState and rebuild
    perfLog("[Coordinator] loadInitialState not yet implemented for new system\n");
}

// --- Score ---

void PerformanceCoordinator::replayScore(int upToStep) {
    auto steps = stateAPI->scoreSteps();
    if (steps.empty()) {
        perfLog("[Coordinator] No score steps to replay\n");
        return;
    }

    // TODO: load initial state first, then replay
    int count = (upToStep < 0) ? (int)steps.size() : std::min(upToStep, (int)steps.size());
    perfLog("[Coordinator] Replaying score: %d of %d steps\n", count, (int)steps.size());

    for (int i = 0; i < count; ++i) {
        auto& step = steps[i];
        auto* action = stateAPI->findActionById(step.actionId);
        if (!action) continue;
        auto args = juce::JSON::parse(juce::String(step.args));
        executeAction(action->name, args, 1.0f);
        perfLog("[Coordinator] Score step %d: %s\n", i + 1, step.description.c_str());
    }
}

// --- Track presets ---

static juce::File getTrackPresetsDir() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".config/performance/track_presets");
}

void PerformanceCoordinator::saveTrackPreset(const juce::String& trackId,
                                              const juce::String& presetName) {
    auto* body = new juce::DynamicObject();

    auto pluginName = juce::String(stateAPI->getTrackPluginName(trackId.toStdString()));
    body->setProperty("plugin", pluginName);

    // Instrument state from engine
    if (auto* proc = audioEngine->getTrackInstrumentProcessor(trackId)) {
        juce::MemoryBlock memState;
        proc->getStateInformation(memState);
        body->setProperty("pluginState", memState.toBase64Encoding());
    }

    // Effects from state + engine processor state
    juce::Array<juce::var> effectsArr;
    for (auto& fx : stateAPI->getTrackEffects(trackId.toStdString())) {
        auto* fxObj = new juce::DynamicObject();
        fxObj->setProperty("plugin", juce::String(fx.pluginName));
        if (auto* proc = audioEngine->getEffectProcessor(trackId, juce::String(fx.effectId))) {
            juce::MemoryBlock memState;
            proc->getStateInformation(memState);
            fxObj->setProperty("state", memState.toBase64Encoding());
        }
        effectsArr.add(juce::var(fxObj));
    }
    body->setProperty("effects", effectsArr);

    // Sends from state
    juce::Array<juce::var> sendsArr;
    for (auto& send : stateAPI->getTrackSends(trackId.toStdString())) {
        auto* sendObj = new juce::DynamicObject();
        sendObj->setProperty("bus", juce::String(send.busName));
        sendObj->setProperty("gain", send.gain);
        sendsArr.add(juce::var(sendObj));
    }
    body->setProperty("sends", sendsArr);

    body->setProperty("gain", stateAPI->getTrackGain(trackId.toStdString()));
    body->setProperty("midiEnabled", stateAPI->isTrackMidiEnabled(trackId.toStdString()));

    auto dir = getTrackPresetsDir();
    dir.createDirectory();
    auto file = dir.getChildFile(presetName + ".json");
    file.replaceWithText(juce::JSON::toString(juce::var(body), true));

    perfLog("[Coordinator] Saved track preset \"%s\"\n", presetName.toRawUTF8());
}

void PerformanceCoordinator::loadTrackPreset(const juce::String& trackId,
                                              const juce::String& presetName) {
    auto file = getTrackPresetsDir().getChildFile(presetName + ".json");
    if (!file.existsAsFile()) return;

    auto json = juce::JSON::parse(file.loadFileAsString());
    auto pluginName = json.getProperty("plugin", "").toString();

    if (pluginName.isNotEmpty()) {
        stateAPI->clearTrackPlugin(trackId.toStdString());
        auto* plugin = stateAPI->findPluginByName(pluginName.toStdString());
        if (plugin)
            stateAPI->setTrackPlugin(trackId.toStdString(), plugin->id);

        // Store captured state on the track — EngineSync restores it
        // automatically when the plugin finishes async loading (LoadStatus → Loaded)
        auto stateB64 = json.getProperty("pluginState", "").toString();
        if (stateB64.isNotEmpty()) {
            auto* track = stateAPI->findTrack(trackId.toStdString());
            if (track) {
                track->processorState = stateB64.toStdString();
                track->processorStateHash.clear();  // will be set on next capture
            }
        }
    }

    // Effects
    if (auto* effectsArr = json.getProperty("effects", juce::var()).getArray()) {
        for (auto& fx : stateAPI->getTrackEffects(trackId.toStdString()))
            stateAPI->removeEffect(fx.effectId);

        for (auto& fxVar : *effectsArr) {
            auto fxPlugin = fxVar.getProperty("plugin", "").toString();
            if (fxPlugin.isNotEmpty()) {
                auto* plugin = stateAPI->findPluginByName(fxPlugin.toStdString());
                if (plugin)
                    stateAPI->addEffect(trackId.toStdString(), fxPlugin.toStdString(), plugin->id);
            }
        }
    }

    stateAPI->setTrackGain(trackId.toStdString(), (float)json.getProperty("gain", 1.0));
    stateAPI->setTrackMidiEnabled(trackId.toStdString(), (bool)json.getProperty("midiEnabled", true));
    stateAPI->renameTrack(trackId.toStdString(), presetName.toStdString());

    perfLog("[Coordinator] Loaded track preset \"%s\"\n", presetName.toRawUTF8());
}

std::vector<juce::String> PerformanceCoordinator::listTrackPresets() {
    std::vector<juce::String> names;
    auto dir = getTrackPresetsDir();
    if (!dir.isDirectory()) return names;
    for (auto& entry : juce::RangedDirectoryIterator(dir, false, "*.json"))
        names.push_back(entry.getFile().getFileNameWithoutExtension());
    std::sort(names.begin(), names.end());
    return names;
}

// --- Automation ---

int PerformanceCoordinator::interpolate(float from, float to, float durationSec,
                                         AutomationCallback callback, EasingFn easing) {
    return automationEngine->interpolate(from, to, durationSec, std::move(callback), std::move(easing));
}

int PerformanceCoordinator::delay(float delaySec, std::function<void()> callback) {
    return automationEngine->delay(delaySec, std::move(callback));
}

void PerformanceCoordinator::cancelAutomation(int handle) {
    automationEngine->cancel(handle);
}

void PerformanceCoordinator::cancelAllAutomation() {
    automationEngine->cancelAll();
}

// --- Action dispatch ---

static MIDIControl::Type parseControlType(const juce::String& type) {
    if (type.equalsIgnoreCase("cc")) return MIDIControl::CC;
    if (type.equalsIgnoreCase("note")) return MIDIControl::Note;
    if (type.equalsIgnoreCase("pitchbend")) return MIDIControl::PitchBend;
    if (type.equalsIgnoreCase("pressure")) return MIDIControl::Pressure;
    return MIDIControl::CC;
}

void PerformanceCoordinator::executeAction(const std::string& actionName,
                                            const juce::var& args, float value) {
    if (value == 0.0f) return;

    // Check for custom Lua action first
    auto* actionInfo = stateAPI->findActionByName(actionName);
    if (actionInfo && !actionInfo->luaCode.empty() && luaExecutor) {
        perfLog("[Coordinator] Executing custom action: %s\n", actionName.c_str());
        auto result = luaExecutor(actionInfo->luaCode);
        if (!result.empty() && result != "ok")
            perfLog("[Coordinator] Custom action error: %s\n", result.c_str());
        return;
    }

    auto getArg = [&](int index) -> juce::String {
        if (auto* arr = args.getArray())
            if (index < arr->size())
                return (*arr)[index].toString();
        return {};
    };
    auto getArgFloat = [&](int index, float def = 0.0f) -> float {
        if (auto* arr = args.getArray())
            if (index < arr->size())
                return (float)(*arr)[index];
        return def;
    };

    auto resolveTrack = [this](const juce::String& id) -> std::string {
        auto s = id.toStdString();
        if (!stateAPI->findTrack(s))
            perfLog("[Coordinator] resolveTrack: '%s' not found\n", s.c_str());
        return s;
    };

    if (actionName == "setActiveTrack") {
        auto targetId = resolveTrack(getArg(0));
        for (auto& t : stateAPI->listTracks()) {
            bool active = (t.id == targetId);
            stateAPI->setTrackMidiEnabled(t.id, active);
            stateAPI->setTrackAudioEnabled(t.id, active);
        }
    }
    else if (actionName == "enableTrack") {
        auto id = resolveTrack(getArg(0));
        stateAPI->setTrackMidiEnabled(id, true);
        stateAPI->setTrackAudioEnabled(id, true);
    }
    else if (actionName == "disableTrack") {
        auto id = resolveTrack(getArg(0));
        stateAPI->setTrackMidiEnabled(id, false);
        stateAPI->setTrackAudioEnabled(id, false);
    }
    else if (actionName == "fadeOut") {
        auto track = resolveTrack(getArg(0));
        auto dur = getArgFloat(1, 3.0f);
        float current = stateAPI->getTrackGain(track);
        automationEngine->interpolate(current, 0.0f, dur,
            [this, track](float v) { stateAPI->setTrackGain(track, v); },
            AutomationEngine::easingByName(getArg(2).toStdString()));
    }
    else if (actionName == "fadeIn") {
        auto track = resolveTrack(getArg(0));
        auto dur = getArgFloat(1, 3.0f);
        float current = stateAPI->getTrackGain(track);
        automationEngine->interpolate(current, 1.0f, dur,
            [this, track](float v) { stateAPI->setTrackGain(track, v); },
            AutomationEngine::easingByName(getArg(2).toStdString()));
    }
    else if (actionName == "crossfade") {
        auto from = resolveTrack(getArg(0));
        auto to = resolveTrack(getArg(1));
        auto dur = getArgFloat(2, 3.0f);
        auto easing = AutomationEngine::easingByName(getArg(3).toStdString());
        automationEngine->interpolate(1.0f, 0.0f, dur,
            [this, from](float v) { stateAPI->setTrackGain(from, v); }, easing);
        automationEngine->interpolate(0.0f, 1.0f, dur,
            [this, to](float v) { stateAPI->setTrackGain(to, v); }, easing);
    }
    else {
        perfLog("[Action] Unknown action: %s\n", actionName.c_str());
    }
}

void PerformanceCoordinator::refreshMidiDevices() {
    if (midiEngine) midiEngine->refreshDeviceMapping();
}

void PerformanceCoordinator::startMidiLearn(const std::string& deviceId,
    std::function<void(const std::string& controlType, int channel, int number)> callback) {
    if (midiEngine) midiEngine->startLearn(deviceId, std::move(callback));
}

void PerformanceCoordinator::cancelMidiLearn() {
    if (midiEngine) midiEngine->cancelLearn();
}

void PerformanceCoordinator::setMidiDeviceMonitor(const std::string& deviceId,
    std::function<void(const std::string& description,
                       const std::string& type, int channel, int number)> callback) {
    if (midiEngine) midiEngine->setDeviceMonitor(deviceId, std::move(callback));
}

void PerformanceCoordinator::clearMidiDeviceMonitor() {
    if (midiEngine) midiEngine->clearDeviceMonitor();
}

void PerformanceCoordinator::setGlobalMidiMonitor(
    std::function<void(const std::string& deviceName, const std::string& description,
                       const std::string& type, int channel, int number, int value)> callback) {
    if (midiEngine) midiEngine->setGlobalMonitor(std::move(callback));
}

void PerformanceCoordinator::clearGlobalMidiMonitor() {
    if (midiEngine) midiEngine->clearGlobalMonitor();
}

void PerformanceCoordinator::log(const juce::String& message) {
    perfLog("[Coordinator] %s\n", message.toRawUTF8());
}

// --- Internal ---

// --- State event handler: auto-create Default preset ---

void PerformanceCoordinator::onStateEvent(const StateEvent& event) {
    // Binding changes — rebuild runtime dispatch map
    if (event.entity == StateEvent::Binding) {
        restoreBindings();
        return;
    }

    // Watch for Track or Effect Updated events — LoadStatus may have changed to Loaded
    if (event.action != StateEvent::Updated) return;

    if (event.entity == StateEvent::Track) {
        auto* track = stateAPI->findTrack(event.entityId);
        if (!track || track->instrumentLoadStatus != LoadStatus::Loaded) return;
        if (track->pluginId.empty()) return;
        ensureDefaultPreset(track->id, "", track->pluginId, PresetKind::Instrument);
    }
    else if (event.entity == StateEvent::Effect) {
        auto* fx = stateAPI->findEffect(event.entityId);
        if (!fx || fx->loadStatus != LoadStatus::Loaded) return;
        if (fx->pluginId.empty()) return;
        ensureDefaultPreset(event.parentId, fx->id, fx->pluginId, PresetKind::Effect);
    }
}

void PerformanceCoordinator::ensureDefaultPreset(const std::string& parentId,
                                                   const std::string& effectId,
                                                   const std::string& pluginId,
                                                   PresetKind kind) {
    // Already has a Default preset?
    if (stateAPI->findPreset(pluginId, "Default")) return;

    auto* plugin = stateAPI->findPluginById(pluginId);
    if (!plugin) return;

    // Get the processor and capture its initial state
    juce::AudioProcessor* proc = nullptr;
    auto engParent = (parentId == stateAPI->getMasterOutputId())
                         ? juce::String("Output") : juce::String(parentId);
    if (effectId.empty())
        proc = audioEngine->getTrackInstrumentProcessor(juce::String(parentId));
    else
        proc = audioEngine->getEffectProcessor(engParent, juce::String(effectId));
    if (!proc) return;

    juce::MemoryBlock state;
    proc->getStateInformation(state);
    if (state.getSize() == 0) return;

    // Save to disk
    auto dir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile(".config/performance/snapshots")
                   .getChildFile(juce::String(plugin->name));
    dir.createDirectory();
    auto file = dir.getChildFile("Default.state");
    file.replaceWithData(state.getData(), state.getSize());

    // Register in state
    stateAPI->createPreset(pluginId, "Default", file.getFullPathName().toStdString(), kind);

    perfLog("[Coordinator] Created Default preset for %s (%d bytes)\n",
            plugin->name.c_str(), (int)state.getSize());
}

void PerformanceCoordinator::populatePluginCatalog() {
    int count = 0;
    for (auto& type : audioEngine->getKnownPlugins().getTypes()) {
        stateAPI->registerPlugin(
            type.name.toStdString(),
            type.manufacturerName.toStdString(),
            type.fileOrIdentifier.toStdString(),
            type.isInstrument);
        count++;
    }
    perfLog("[Coordinator] Registered %d plugins in catalog\n", count);
}

void PerformanceCoordinator::registerBuiltinActions() {
    stateAPI->registerAction("setActiveTrack", "Set active track",
        R"([{"name":"trackName","type":"string"}])");
    stateAPI->registerAction("enableTrack", "Enable track",
        R"([{"name":"trackName","type":"string"}])");
    stateAPI->registerAction("disableTrack", "Disable track",
        R"([{"name":"trackName","type":"string"}])");
    stateAPI->registerAction("fadeOut", "Fade out",
        R"([{"name":"trackName","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])");
    stateAPI->registerAction("fadeIn", "Fade in",
        R"([{"name":"trackName","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])");
    stateAPI->registerAction("crossfade", "Crossfade",
        R"([{"name":"fromTrack","type":"string"},{"name":"toTrack","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])");

    perfLog("[Coordinator] Registered %d built-in actions\n", (int)stateAPI->allActions().size());
}

void PerformanceCoordinator::restoreBindings() {
    songRuntime->clearBindings();

    for (auto& binding : stateAPI->effectiveBindings()) {
        auto* action = stateAPI->findActionById(binding.actionId);
        if (!action) continue;

        auto actionNameStr = action->name;
        auto argsStr = binding.args;
        MIDIControl control = { parseControlType(juce::String(binding.controlType)),
                                binding.channel, binding.number, binding.deviceId };

        perfLog("[Coordinator] Binding: %s ch%d #%d dev='%s' -> %s\n",
                binding.controlType.c_str(), binding.channel, binding.number,
                binding.deviceId.c_str(), actionNameStr.c_str());

        songRuntime->addBinding(control, [this, actionNameStr, argsStr](float value) {
            perfLog("[Coordinator] Action triggered: %s (value=%.2f)\n", actionNameStr.c_str(), value);
            auto args = juce::JSON::parse(juce::String(argsStr));
            executeAction(actionNameStr, args, value);
        }, juce::String(binding.description));
    }
}
