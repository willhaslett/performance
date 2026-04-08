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
#include "song/Song.h"

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

    // Load persisted state first — establishes plugin/action IDs from DB
    persistence->loadInto(*stateAPI);

    // Then populate from engine scan — deduplicates by name, keeps DB IDs
    populatePluginCatalog();

    // Register built-in actions — deduplicates by name, keeps DB IDs
    registerBuiltinActions();

    // Engine sync — state events drive the engine
    engineSync = std::make_unique<EngineSync>(*audioEngine, *stateAPI);

    automationEngine = std::make_unique<AutomationEngine>();
    songRuntime = std::make_unique<SongRuntime>(*audioEngine);

    midiEngine = std::make_unique<MIDIEngine>(
        audioEngine->getDeviceManager(), *audioEngine);
    midiEngine->setSongRuntime(songRuntime.get());
    midiEngine->setMonitorMode(true);
    midiEngine->initialise();
    perfLog("[Coordinator] MIDIEngine initialised\n");
}

void PerformanceCoordinator::shutdown() {
    if (stateAPI && persistence && stateAPI->isDirty())
        persistence->saveFrom(*stateAPI);
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

void PerformanceCoordinator::save() {
    if (persistence && stateAPI)
        persistence->saveFrom(*stateAPI);
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

void PerformanceCoordinator::saveScore(const std::vector<std::string>& stepDescriptions) {
    // TODO
    perfLog("[Coordinator] saveScore not yet implemented for new system\n");
}

void PerformanceCoordinator::replayScore(int upToStep) {
    // TODO
    perfLog("[Coordinator] replayScore not yet implemented for new system\n");
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

        // Restore instrument state after plugin loads
        auto stateB64 = json.getProperty("pluginState", "").toString();
        if (stateB64.isNotEmpty()) {
            auto stableId = trackId;
            juce::Timer::callAfterDelay(500, [this, stableId, stateB64] {
                if (auto* proc = audioEngine->getTrackInstrumentProcessor(stableId)) {
                    juce::MemoryBlock memState;
                    memState.fromBase64Encoding(stateB64);
                    proc->setStateInformation(memState.getData(), (int)memState.getSize());
                }
            });
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

    auto sid = [](const juce::String& s) { return s.toStdString(); };

    if (actionName == "setActiveTrack") {
        auto targetId = getArg(0);
        for (auto& t : stateAPI->listTracks())
            stateAPI->setTrackMidiEnabled(t.id, t.id == sid(targetId));
    }
    else if (actionName == "enableTrack") {
        stateAPI->setTrackMidiEnabled(sid(getArg(0)), true);
    }
    else if (actionName == "disableTrack") {
        stateAPI->setTrackMidiEnabled(sid(getArg(0)), false);
    }
    else if (actionName == "fadeOut") {
        auto track = sid(getArg(0));
        auto dur = getArgFloat(1, 3.0f);
        float current = stateAPI->getTrackGain(track);
        automationEngine->interpolate(current, 0.0f, dur,
            [this, track](float v) { stateAPI->setTrackGain(track, v); },
            AutomationEngine::easingByName(sid(getArg(2))));
    }
    else if (actionName == "fadeIn") {
        auto track = sid(getArg(0));
        auto dur = getArgFloat(1, 3.0f);
        float current = stateAPI->getTrackGain(track);
        automationEngine->interpolate(current, 1.0f, dur,
            [this, track](float v) { stateAPI->setTrackGain(track, v); },
            AutomationEngine::easingByName(sid(getArg(2))));
    }
    else if (actionName == "crossfade") {
        auto from = sid(getArg(0));
        auto to = sid(getArg(1));
        auto dur = getArgFloat(2, 3.0f);
        auto easing = AutomationEngine::easingByName(sid(getArg(3)));
        automationEngine->interpolate(1.0f, 0.0f, dur,
            [this, from](float v) { stateAPI->setTrackGain(from, v); }, easing);
        automationEngine->interpolate(0.0f, 1.0f, dur,
            [this, to](float v) { stateAPI->setTrackGain(to, v); }, easing);
    }
    else {
        perfLog("[Action] Unknown action: %s\n", actionName.c_str());
    }
}

void PerformanceCoordinator::log(const juce::String& message) {
    perfLog("[Coordinator] %s\n", message.toRawUTF8());
}

// --- Internal ---

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
                                binding.channel, binding.number };

        songRuntime->addBinding(control, [this, actionNameStr, argsStr](float value) {
            auto args = juce::JSON::parse(juce::String(argsStr));
            executeAction(actionNameStr, args, value);
        }, juce::String(binding.description));
    }
}
