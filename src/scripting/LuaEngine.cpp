#include "scripting/LuaEngine.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "automation/AutomationEngine.h"
#include "state/ParamSchemaJson.h"
#include "engine/Log.h"
#include "composer/ComposerOutput.h"
#include "composer/NotationParser.h"
#include "composer/ComposerWriter.h"
#include <juce_events/juce_events.h>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

static std::string getLuaLibDirectory() {
    auto home = std::string(getenv("HOME"));
    return home + "/.config/performance/lua_lib";
}

LuaEngine::LuaEngine(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator)
    : stateRef(state), engineRef(engine), coordRef(coordinator) {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    registerAPI();
    loadLibraries();
}

void LuaEngine::loadLibraries() {
    auto libDir = getLuaLibDirectory();
    if (!fs::exists(libDir)) return;

    for (auto& entry : fs::directory_iterator(libDir)) {
        if (entry.path().extension() == ".lua") {
            auto result = lua.safe_script_file(entry.path().string(), sol::script_pass_on_error);
            if (result.valid())
                perfLog("[Lua] Loaded library: %s\n", entry.path().filename().c_str());
            else {
                sol::error err = result;
                perfLog("[Lua] ERROR loading library %s: %s\n",
                        entry.path().filename().c_str(), err.what());
            }
        }
    }
}

void LuaEngine::registerAPI() {
    auto& state = stateRef;
    auto& engine = engineRef;
    auto& coord = coordRef;

    // Name resolution helpers. Throw on failure — sol2 converts the exception
    // into a Lua error, which propagates back to Claude via executeString as
    // "error: ...". That's what lets Claude self-correct (e.g. retry with a
    // name it got from a query call).
    auto resolveTrackId = [&state](const std::string& name) -> std::string {
        auto id = state.findTrackIdByName(name);
        if (id.empty())
            throw std::runtime_error("track '" + name + "' not found. Run registryList('track') to see available names.");
        return id.str();
    };
    auto resolveBusId = [&state](const std::string& name) -> std::string {
        auto id = state.findBusIdByName(name);
        if (id.empty())
            throw std::runtime_error("bus '" + name + "' not found. Run registryList('bus') to see available names.");
        return id.str();
    };
    auto resolveParentId = [&state](const std::string& name) -> std::string {
        if (name == "Output") return state.getMasterOutputId();
        auto tid = state.findTrackIdByName(name);
        if (!tid.empty()) return tid.str();
        auto bid = state.findBusIdByName(name);
        if (!bid.empty()) return bid.str();
        throw std::runtime_error("'" + name + "' is not a track, bus, or 'Output'. Run registryList('track') and registryList('bus') for available names.");
    };

    // Song
    lua.set_function("song", [&coord](const std::string& name) {
        coord.createSong(juce::String(name));
    });

    // Track management
    lua.set_function("createTrack", [&state](const std::string& name) -> std::string {
        return state.createTrack(name).str();
    });
    // Validates an input channel selection against the current device. Accepts
    // the "no live input" sentinel (start=-1, count=0). Throws a Lua error
    // (which surfaces in the Claude tool result, prompting self-correction)
    // when the selection is out of range — otherwise the engine silently
    // accepts an invalid config and the track ends up routed to a non-existent
    // channel.
    auto validateInputChannels = [&engine](int start, int count) {
        if (start == -1 && count == 0) return;  // sentinel: no live input
        int available = (int)engine.getInputChannelNames().size();
        if (count <= 0)
            throw std::runtime_error("input channel count must be positive (got " +
                                     std::to_string(count) + ")");
        if (start < 0 || start + count > available)
            throw std::runtime_error("input channels [" + std::to_string(start) + ".." +
                                     std::to_string(start + count - 1) +
                                     "] out of range; device has " + std::to_string(available) +
                                     " channel(s) (0-indexed). Call listInputChannels() to see what's available.");
    };

    lua.set_function("createAudioInputTrack", [&state, validateInputChannels](const std::string& name,
                                                          int inputStart, int inputCount) -> std::string {
        validateInputChannels(inputStart, inputCount);
        return state.createAudioInputTrack(name, inputStart, inputCount).str();
    });
    lua.set_function("setTrackInputChannels", [&state, resolveTrackId, validateInputChannels](const std::string& track,
                                                int start, int count) {
        validateInputChannels(start, count);
        state.setTrackInputChannels(TrackId{resolveTrackId(track)}, start, count);
    });
    lua.set_function("listInputChannels", [this, &engine]() -> sol::table {
        auto names = engine.getInputChannelNames();
        sol::table result = lua.create_table();
        for (size_t i = 0; i < names.size(); ++i)
            result[i + 1] = names[i].toStdString();
        return result;
    });
    lua.set_function("listAudioDevices", [this, &engine]() -> sol::table {
        auto& dm = engine.getDeviceManager();
        sol::table result = lua.create_table();
        if (auto* type = dm.getCurrentDeviceTypeObject()) {
            auto devices = type->getDeviceNames();
            for (int i = 0; i < devices.size(); ++i)
                result[i + 1] = devices[i].toStdString();
        }
        return result;
    });
    lua.set_function("setAudioDevice", [&engine, &state](const std::string& name) {
        auto deviceName = juce::String(name);
        state.setConfig("audio_output_device", name);
        juce::MessageManager::callAsync([&engine, deviceName]() {
            auto& dm = engine.getDeviceManager();
            auto setup = dm.getAudioDeviceSetup();
            setup.outputDeviceName = deviceName;
            dm.setAudioDeviceSetup(setup, true);
        });
    });
    lua.set_function("setAudioInputDevice", [&engine, &state](const std::string& name) {
        auto deviceName = juce::String(name);
        state.setConfig("audio_input_device", name);
        juce::MessageManager::callAsync([&engine, deviceName]() {
            auto& dm = engine.getDeviceManager();
            auto setup = dm.getAudioDeviceSetup();
            setup.inputDeviceName = deviceName;
            dm.setAudioDeviceSetup(setup, true);
        });
    });
    lua.set_function("removeTrack", [&state, resolveTrackId](const std::string& name) {
        state.removeTrack(TrackId{resolveTrackId(name)});
    });
    lua.set_function("addInstrument", [&state, resolveTrackId](const std::string& track,
                                       const std::string& plugin, sol::optional<std::string> preset) {
        auto trackId = TrackId{resolveTrackId(track)};
        auto* p = state.findPluginByName(plugin);
        if (!p)
            throw std::runtime_error("plugin '" + plugin + "' not found. Run listPlugins() for available names.");
        PresetId presetId;
        if (preset.has_value() && !preset.value().empty()) {
            auto* pr = state.findPreset(p->id, preset.value());
            if (pr) presetId = pr->id;
        }
        state.setTrackPlugin(trackId, p->id, presetId);
    });
    lua.set_function("addEffect", [&state, resolveParentId](const std::string& parent,
                                    const std::string& effectName, const std::string& plugin) {
        auto* p = state.findPluginByName(plugin);
        if (!p)
            throw std::runtime_error("plugin '" + plugin + "' not found. Run listPlugins() for available names.");
        state.addEffect(resolveParentId(parent), effectName, p->id);
    });
    lua.set_function("addTrackEffect", [&state, resolveParentId](const std::string& parent,
                                         const std::string& effectName, const std::string& plugin) {
        auto* p = state.findPluginByName(plugin);
        if (!p)
            throw std::runtime_error("plugin '" + plugin + "' not found. Run listPlugins() for available names.");
        state.addEffect(resolveParentId(parent), effectName, p->id);
    });
    lua.set_function("addBusEffect", [&state, resolveParentId](const std::string& parent,
                                       const std::string& effectName, const std::string& plugin) {
        auto* p = state.findPluginByName(plugin);
        if (!p)
            throw std::runtime_error("plugin '" + plugin + "' not found. Run listPlugins() for available names.");
        state.addEffect(resolveParentId(parent), effectName, p->id);
    });
    lua.set_function("removeEffect", [&state](const std::string&, const std::string& effectId) {
        state.removeEffect(EffectId{effectId});
    });
    lua.set_function("setTrackInputMonitoring", [&state, resolveTrackId](const std::string& track, bool enabled) {
        state.setTrackInputMonitoring(TrackId{resolveTrackId(track)}, enabled);
    });
    lua.set_function("setTrackGain", [&state, resolveTrackId](const std::string& track, float gain) {
        state.setTrackGain(TrackId{resolveTrackId(track)}, gain);
    });
    lua.set_function("setMasterGain", [&state](float gain) {
        state.setMasterGain(gain);
    });
    lua.set_function("selectTrack", [&state, resolveTrackId](const std::string& track) {
        state.selectTrack(TrackId{resolveTrackId(track)}, false);
    });
    // setChannelGain — UUID-first setter for track / bus / "Main"|"output".
    // Used in action-body Lua where args are already UUID-typed refs.
    lua.set_function("setChannelGain", [&state](const std::string& id, float gain) {
        if (id == "Main" || id == "output") { state.setMasterGain(gain); return; }
        if (state.findTrack(TrackId{id})) { state.setTrackGain(TrackId{id}, gain); return; }
        if (state.findBus(BusId{id}))     { state.setBusGain(BusId{id}, gain); return; }
    });
    lua.set_function("setTrackGainDb", [&state, resolveTrackId](const std::string& track, float db) {
        float linear = (db <= -60.0f) ? 0.0f : std::pow(10.0f, db / 20.0f);
        state.setTrackGain(TrackId{resolveTrackId(track)}, linear);
    });
    lua.set_function("getTrackGain", [&state, resolveTrackId](const std::string& track) -> float {
        return state.getTrackGain(TrackId{resolveTrackId(track)});
    });

    // Bus management
    lua.set_function("createBus", [&state](const std::string& name) -> std::string {
        return state.createBus(name).str();
    });
    lua.set_function("removeBus", [&state, resolveBusId](const std::string& name) {
        state.removeBus(BusId{resolveBusId(name)});
    });
    lua.set_function("setBusGain", [&state, resolveBusId](const std::string& bus, float gain) {
        state.setBusGain(BusId{resolveBusId(bus)}, gain);
    });
    lua.set_function("setBusGainDb", [&state, resolveBusId](const std::string& bus, float db) {
        float linear = (db <= -60.0f) ? 0.0f : std::pow(10.0f, db / 20.0f);
        state.setBusGain(BusId{resolveBusId(bus)}, linear);
    });

    // Sends
    lua.set_function("addSendDb", [&state, resolveTrackId, resolveBusId](const std::string& track,
                                    const std::string& bus, float db) -> std::string {
        float linear = (db <= -60.0f) ? 0.0f : std::pow(10.0f, db / 20.0f);
        return state.addSend(TrackId{resolveTrackId(track)}, BusId{resolveBusId(bus)}, linear).str();
    });
    lua.set_function("setSendGainDb", [&state, resolveTrackId, resolveBusId](const std::string& track,
                                        const std::string& bus, float db) {
        float linear = (db <= -60.0f) ? 0.0f : std::pow(10.0f, db / 20.0f);
        state.setSendGainByBus(TrackId{resolveTrackId(track)}, BusId{resolveBusId(bus)}, linear);
    });
    lua.set_function("addSend", [&state, resolveTrackId, resolveBusId](const std::string& track,
                                  const std::string& bus, sol::optional<float> gain) {
        state.addSend(TrackId{resolveTrackId(track)}, BusId{resolveBusId(bus)}, gain.value_or(1.0f));
    });
    lua.set_function("setSendGain", [&state, resolveTrackId, resolveBusId](const std::string& track,
                                      const std::string& bus, float gain) {
        state.setSendGainByBus(TrackId{resolveTrackId(track)}, BusId{resolveBusId(bus)}, gain);
    });

    // Parameters (engine)
    lua.set_function("setParam", [&engine, resolveTrackId](const std::string& track,
                                   const std::string& param, float value) {
        engine.setParam(juce::String(resolveTrackId(track)), juce::String(param), value);
    });
    lua.set_function("setEffectParam", [&engine, resolveParentId](const std::string& parent,
                                         const std::string& effect, const std::string& param, float value) {
        engine.setEffectParam(juce::String(resolveParentId(parent)), juce::String(effect),
                              juce::String(param), value);
    });
    lua.set_function("getParam", [&engine, resolveTrackId](const std::string& track,
                                   const std::string& param) -> float {
        return engine.getParam(juce::String(resolveTrackId(track)), juce::String(param));
    });
    lua.set_function("getEffectParam", [&engine, resolveParentId](const std::string& parent,
                                         const std::string& effect, const std::string& param) -> float {
        return engine.getEffectParam(juce::String(resolveParentId(parent)), juce::String(effect),
                                      juce::String(param));
    });

    // Automation
    lua.set_function("interpolate", [&coord](float from, float to, float duration,
                                              sol::function callback, sol::optional<sol::object> easing) -> int {
        auto luaCallback = callback;
        PerformanceCoordinator::EasingFn easingFn = nullptr;
        if (easing.has_value()) {
            auto& val = easing.value();
            if (val.is<std::string>())
                easingFn = AutomationEngine::easingByName(val.as<std::string>());
            else if (val.is<sol::function>()) {
                auto luaEasing = val.as<sol::function>();
                easingFn = [luaEasing](float t) mutable -> float { return luaEasing(t); };
            }
        }
        return coord.interpolate(from, to, duration,
                                  [luaCallback](float value) mutable { luaCallback(value); },
                                  std::move(easingFn));
    });
    lua.set_function("delay", [&coord](float seconds, sol::function callback) -> int {
        auto luaCallback = callback;
        return coord.delay(seconds, [luaCallback]() mutable { luaCallback(); });
    });
    lua.set_function("cancel", [&coord](int handle) { coord.cancelAutomation(handle); });
    lua.set_function("cancelAll", [&coord]() { coord.cancelAllAutomation(); });

    // Presets (engine)
    lua.set_function("savePreset", [&engine, resolveTrackId](const std::string& track,
                                     const std::string& name) {
        engine.savePreset(juce::String(resolveTrackId(track)), "", juce::String(name));
    });
    lua.set_function("loadPreset", [&engine, resolveTrackId](const std::string& track,
                                     const std::string& name) {
        engine.loadPreset(juce::String(resolveTrackId(track)), "", juce::String(name));
    });
    lua.set_function("morphToPreset", [&coord, resolveTrackId](const std::string& track,
                                       const std::string& preset, float duration,
                                       const std::string& easing) {
        auto trackId = resolveTrackId(track);
        juce::Array<juce::var> arr;
        arr.add(juce::String(trackId));
        arr.add(juce::String(preset));
        arr.add(duration);
        arr.add(juce::String(easing));
        coord.executeAction("morphToPreset", juce::var(arr), 1.0f);
    });
    lua.set_function("listPresets", [this, &engine](const std::string& plugin) -> sol::table {
        auto names = engine.listPresets(juce::String(plugin));
        sol::table result = lua.create_table();
        for (size_t i = 0; i < names.size(); ++i)
            result[i + 1] = names[i].toStdString();
        return result;
    });

    // Plugin UI (engine)
    lua.set_function("openEditor", [&engine, resolveParentId](const std::string& parent,
                                     sol::optional<std::string> effect) {
        engine.openPluginEditor(juce::String(resolveParentId(parent)),
                                juce::String(effect.value_or("")));
    });

    // Query
    lua.set_function("listPlugins", [this, &engine]() -> sol::table {
        auto plugins = engine.listPlugins();
        sol::table result = lua.create_table();
        for (size_t i = 0; i < plugins.size(); ++i)
            result[i + 1] = plugins[i].toStdString();
        return result;
    });

    // Song management
    lua.set_function("listSongs", [this]() -> sol::table {
        auto songs = listSongs();
        sol::table result = lua.create_table();
        for (size_t i = 0; i < songs.size(); ++i)
            result[i + 1] = songs[i];
        return result;
    });
    lua.set_function("loadSong", [this](const std::string& name) {
        auto path = getSongsDirectory() + "/" + name + ".lua";
        juce::MessageManager::callAsync([this, path] { loadSong(path); });
    });
    lua.set_function("unloadSong", [this]() {
        juce::MessageManager::callAsync([this] { unloadSong(); });
    });
    lua.set_function("save", [&coord]() { coord.save(); });

    // Live looping (see docs/LIVE_LOOPING.md + docs/PANE_MODE_MODEL.md).
    // Mode is an AppState-level enum. setMode("looper"|"arrangement")
    // flips it; getMode() returns the current string. Cycle invariants
    // are enforced in StateAPI::setMode / setCycleLength.
    lua.set_function("setMode", [&state](const std::string& mode) {
        if (mode == "looper")           state.setMode(AppMode::Looper);
        else if (mode == "arrangement") state.setMode(AppMode::Arrangement);
        // Unknown values are silently ignored to match the rest of the
        // Lua API's tolerance — callers can check getMode() if they
        // need confirmation.
    });
    lua.set_function("getMode", [&state]() -> std::string {
        return state.getMode() == AppMode::Looper ? "looper" : "arrangement";
    });
    lua.set_function("setCycleLength", [&state](double beats) {
        state.setCycleLength(beats);
    });
    lua.set_function("getCycleLength", [&state]() -> double {
        return state.getCycleLength();
    });
    lua.set_function("setPendingTake", [&state](const std::string& regionId,
                                                  const std::string& takeId) {
        state.setPendingTake(RegionId{regionId}, TakeId{takeId});
    });

    // Phase 6 — performer-facing per-track gestures. All target the
    // currently-focused track; no-op if no focus. Queued at gesture
    // time, fire at the next cycle wrap, capture for one cycle, then
    // commit. See docs/LIVE_INPUT_AND_FOCUS.md.
    lua.set_function("replaceLoop", [&coord]() { coord.replaceLoopGesture(); });
    lua.set_function("overdubLoop", [&coord]() { coord.overdubLoopGesture(); });
    // Looper undo/redo deliberately call app-level undo — same backing
    // history as Cmd-Z, so cycle length / lengthBeats / events are
    // restored together.
    lua.set_function("undoLoop",    [&state]() { state.undo(); });
    lua.set_function("redoLoop",    [&state]() { state.redo(); });
    lua.set_function("clearLoop",   [&state]() { state.clearLoop(); });
    lua.set_function("clearAllLoops", [&state]() { state.clearAllLoops(); });
    lua.set_function("resetLooperSession", [&coord]() { coord.resetLooperSession(); });

    // Transport + focus + focused-track mute. Useful alongside the
    // looper gestures, but mode-agnostic — they work in Producer too.
    lua.set_function("togglePlay",      [&coord]() { coord.togglePlay(); });
    lua.set_function("focusPrevTrack",  [&state]() { state.focusPrevTrack(); });
    lua.set_function("focusNextTrack",  [&state]() { state.focusNextTrack(); });
    lua.set_function("toggleFocusedMute", [&state]() { state.toggleFocusedMute(); });
    lua.set_function("getLoopActionState", [&state]() -> std::string {
        auto tid = state.getFocusedTrackId();
        if (tid.empty()) return "no-focus";
        switch (state.getLoopAction(tid)) {
            case LoopAction::None:             return "none";
            case LoopAction::CapturingReplace: return "capturing-replace";
            case LoopAction::CapturingOverdub: return "capturing-overdub";
        }
        return "unknown";
    });

    // Offline render (bounce) to a stereo WAV file. Returns a one-line
    // human-readable status string so the caller (Claude or dev console)
    // knows wall-clock duration and audio length.
    //
    // Two forms:
    //   bounce(path)                       — use the active cycle region
    //                                        (errors if cycle mode is off)
    //   bounce(path, startBeat, endBeat)   — explicit range
    lua.set_function("bounce", [&coord](const std::string& path,
                                         sol::optional<double> startBeat,
                                         sol::optional<double> endBeat) -> std::string {
        PerformanceCoordinator::BounceResult result =
            (startBeat.has_value() && endBeat.has_value())
                ? coord.bounce(juce::File(juce::String(path)),
                               startBeat.value(), endBeat.value())
                : coord.bounce(juce::File(juce::String(path)));

        // Throw on failure so the error surfaces through executeString
        // even when the caller wrote `bounce(...)` as a bare statement
        // rather than `return bounce(...)`. Matches the resolver-helper
        // pattern for all name-lookup failures.
        if (!result.ok)
            throw std::runtime_error(std::string("bounce failed: ")
                                     + result.errorMessage.toStdString());
        char buf[320];
        std::snprintf(buf, sizeof(buf),
                      "ok: wrote %.2fs of audio (beats %.2f..%.2f) to %s in %.2fs wall (%.1fx realtime)",
                      result.audioDurationSeconds,
                      result.startBeat, result.endBeat,
                      path.c_str(), result.wallClockSeconds,
                      result.wallClockSeconds > 0 ? result.audioDurationSeconds / result.wallClockSeconds : 0.0);
        return std::string(buf);
    });
    lua.set_function("saveInitialState", [&coord]() { coord.saveInitialState(); });
    lua.set_function("loadInitialState", [&coord]() { coord.loadInitialState(); });

    // Composer: parse a notation string and drop the resulting notes
    // into new regions on the current project's tracks. The notation
    // itself declares which tracks it targets (via its `tracks:`
    // block) — the caller only chooses *where* (startBeat) to place
    // them. Defaults to beat 0 if startBeat is omitted.
    //
    // Returns a one-line status string on success; throws with an
    // actionable message on parse or write failure so chat-side
    // callers (Claude, dev console) self-correct instead of silently
    // succeeding on no-ops.
    //
    // Example:
    //   compose([[
    //     tempo: 96
    //     time_signature: 4/4
    //     tracks:
    //       Piano: 0
    //     bar 1 | C
    //       Piano: beat 1 C4 q mf | beat 3 E4 q mf
    //   ]])
    lua.set_function("compose", [&coord](const std::string& notation,
                                          sol::optional<double> startBeat) -> std::string {
        auto parser = makeDefaultNotationParser();
        ComposerOutput out;
        std::string err;
        if (!parser->parse(juce::String(notation), out, err)) {
            throw std::runtime_error(std::string("compose: parse error: ") + err);
        }

        ComposerWriter writer(coord);
        if (!writer.apply(out, startBeat.value_or(0.0), err)) {
            throw std::runtime_error(std::string("compose: write error: ") + err);
        }

        // Count per-track regions for a compact status line.
        std::set<std::string> tracks;
        for (auto& n : out.notes) tracks.insert(n.trackName);
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "ok: wrote %d note(s) across %d track(s) over %.2f beats at beat %.2f",
                      (int) out.notes.size(),
                      (int) tracks.size(),
                      out.lengthBeats,
                      startBeat.value_or(0.0));
        return std::string(buf);
    });

    // Devices
    lua.set_function("registerDevice", [&state, &coord](const std::string& name, const std::string& portName) -> std::string {
        auto id = state.registerDevice(name, portName);
        coord.refreshMidiDevices();
        return id.str();
    });
    lua.set_function("addDeviceControl", [&state](const std::string& deviceId, const std::string& name,
                                                    const std::string& controlType, int channel, int number,
                                                    sol::optional<std::string> group) {
        state.addDeviceControl(DeviceId{deviceId}, name, controlType, channel, number, group.value_or(""));
    });
    lua.set_function("addDeviceToSong", [&state](const std::string& songId, const std::string& deviceId) {
        state.addDeviceToSong(SongId{songId}, DeviceId{deviceId});
    });
    lua.set_function("listDevices", [this, &state]() -> sol::table {
        sol::table result = lua.create_table();
        for (auto& d : state.allDevices()) {
            sol::table row = lua.create_table();
            row["id"] = d.id.str();
            row["name"] = d.name;
            row["port"] = d.midiPortName;
            result.add(row);
        }
        return result;
    });
    lua.set_function("getDeviceControl", [this, &state](const std::string& deviceName,
                                                         const std::string& controlName) -> sol::table {
        sol::table result = lua.create_table();
        for (auto& d : state.allDevices()) {
            if (d.name == deviceName || d.midiPortName == deviceName) {
                for (auto& ctrl : d.controls) {
                    if (ctrl.name == controlName) {
                        result["type"] = ctrl.controlType;
                        result["channel"] = ctrl.channel;
                        result["number"] = ctrl.number;
                        result["group"] = ctrl.group;
                        result["deviceId"] = d.id;
                        return result;
                    }
                }
            }
        }
        return result;  // empty table if not found
    });
    lua.set_function("listDeviceControls", [this, &state](const std::string& deviceName) -> sol::table {
        sol::table result = lua.create_table();
        for (auto& d : state.allDevices()) {
            if (d.name == deviceName || d.midiPortName == deviceName) {
                for (auto& ctrl : d.controls) {
                    sol::table row = lua.create_table();
                    row["name"] = ctrl.name;
                    row["type"] = ctrl.controlType;
                    row["channel"] = ctrl.channel;
                    row["number"] = ctrl.number;
                    row["group"] = ctrl.group;
                    result.add(row);
                }
                break;
            }
        }
        return result;
    });
    lua.set_function("listMidiInputs", [this, &engine]() -> sol::table {
        auto devices = juce::MidiInput::getAvailableDevices();
        sol::table result = lua.create_table();
        for (auto& d : devices) {
            sol::table row = lua.create_table();
            row["name"] = d.name.toStdString();
            row["id"] = d.identifier.toStdString();
            result.add(row);
        }
        return result;
    });

    // MIDI bindings
    lua.set_function("bind", [this, &state](const std::string& type, int channel, int number,
                                             const std::string& actionName,
                                             sol::optional<sol::object> argsOrDesc,
                                             sol::optional<std::string> descOpt,
                                             sol::optional<std::string> deviceIdOpt) {
        std::string argsJson = "[]";
        std::string description;
        std::string deviceId;
        if (argsOrDesc.has_value()) {
            auto& val = argsOrDesc.value();
            if (val.is<sol::table>()) {
                auto tbl = val.as<sol::table>();
                juce::Array<juce::var> arr;
                for (size_t i = 1; i <= tbl.size(); ++i) {
                    sol::object item = tbl[i];
                    if (item.is<std::string>()) arr.add(juce::var(juce::String(item.as<std::string>())));
                    else if (item.is<double>()) arr.add(juce::var(item.as<double>()));
                    else if (item.is<bool>()) arr.add(juce::var(item.as<bool>()));
                }
                argsJson = juce::JSON::toString(juce::var(arr)).toStdString();
            } else if (val.is<std::string>()) {
                description = val.as<std::string>();
            }
        }
        if (descOpt.has_value()) description = descOpt.value();
        if (deviceIdOpt.has_value()) deviceId = deviceIdOpt.value();

        auto* action = state.findActionByName(actionName);
        if (!action)
            throw std::runtime_error("action '" + actionName + "' not found. Built-in actions: setActiveTrack, enableTrack, disableTrack, fadeOut, fadeIn, crossfade, trackVolume, morphToPreset.");

        auto songId = state.getMasterOutputId();
        if (songId.empty())
            throw std::runtime_error("no active song; cannot bind.");

        // Resolve track-name args to UUIDs at bind time. The typed ParamSchema
        // tells us exactly which arg slots are track references.
        auto argsVar = juce::JSON::parse(juce::String(argsJson));
        if (auto* argsArr = argsVar.getArray()) {
            for (int i = 0; i < std::min((int)argsArr->size(), (int)action->params.size()); ++i) {
                const auto& p = action->params[i];
                bool isTrackOnly = p.type == ParamType::ChannelRef
                    && !p.scope.empty()
                    && std::all_of(p.scope.begin(), p.scope.end(),
                                   [](const std::string& s) { return s == "track"; });
                if (isTrackOnly && (*argsArr)[i].isString()) {
                    auto trackName = (*argsArr)[i].toString();
                    std::string resolved;
                    for (auto& t : state.listTracks())
                        if (juce::String(t.name) == trackName) { resolved = t.id.str(); break; }
                    if (resolved.empty()) {
                        auto lower = trackName.toLowerCase();
                        for (auto& t : state.listTracks())
                            if (juce::String(t.name).toLowerCase() == lower) { resolved = t.id.str(); break; }
                    }
                    if (resolved.empty())
                        throw std::runtime_error(std::string("bind: track '") + trackName.toStdString() + "' not found. Run registryList('track') to see available names.");
                    argsArr->set(i, juce::var(juce::String(resolved)));
                }
            }
            argsJson = juce::JSON::toString(argsVar, true).toStdString();
        }

        state.addBinding(SongId{songId}, type, channel, number, action->id, argsJson, description, DeviceId{deviceId});
        perfLog("[Lua] bind: %s ch%d #%d dev='%s' -> %s args=%s\n",
                type.c_str(), channel, number, deviceId.c_str(), actionName.c_str(), argsJson.c_str());
    });

    // Generic state queries
    lua.set_function("registryList", [this, &state](const std::string& type,
                                                     sol::optional<sol::table>) -> sol::table {
        sol::table result = lua.create_table();
        if (type == "track") {
            for (auto& t : state.listTracks()) {
                sol::table row = lua.create_table();
                row["id"] = t.id.str(); row["name"] = t.name;
                result.add(row);
            }
        } else if (type == "bus") {
            for (auto& b : state.listBusses()) {
                sol::table row = lua.create_table();
                row["id"] = b.id.str(); row["name"] = b.name;
                result.add(row);
            }
        } else if (type == "song") {
            for (auto& s : state.allSongs()) {
                sol::table row = lua.create_table();
                row["id"] = s.id.str(); row["name"] = s.name;
                result.add(row);
            }
        }
        return result;
    });
    lua.set_function("registryDelete", [&state](const std::string& id) {
        if (state.findTrack(TrackId{id})) state.removeTrack(TrackId{id});
        else if (state.findBus(BusId{id})) state.removeBus(BusId{id});
        else if (state.findSong(SongId{id})) state.deleteSong(SongId{id});
    });

    // Custom actions. paramSchemaJson is the same typed grammar documented
    // in runtime/CLAUDE.md — e.g. '[{"name":"track","type":"channelRef","scope":["track"]}]'.
    // Omit for a zero-arg action (just a macro).
    lua.set_function("createAction", [&state](const std::string& name, const std::string& label,
                                               const std::string& luaCode,
                                               sol::optional<std::string> paramSchemaJson,
                                               sol::optional<std::string> songId) -> std::string {
        std::vector<ParamSchema> params;
        if (paramSchemaJson.has_value() && !paramSchemaJson.value().empty())
            params = ParamSchemaJson::fromJson(paramSchemaJson.value());
        SongId sid = songId.has_value() ? SongId{songId.value()} : SongId{};
        auto id = state.createCustomAction(name, label, luaCode, std::move(params), sid);
        perfLog("[Lua] Created custom action: %s (id=%s, %d params)\n",
                name.c_str(), id.c_str(), (int)params.size());
        return id.str();
    });
    lua.set_function("removeAction", [&state](const std::string& id) {
        state.removeAction(ActionId{id});
    });
    lua.set_function("triggerAction", [&coord](const std::string& actionName) {
        coord.executeAction(actionName, juce::var(), 1.0f);
    });
    lua.set_function("currentSongId", [&state]() -> std::string {
        auto* song = state.currentSong();
        return song ? song->id.str() : std::string{};
    });

    // Utility
    lua.set_function("log", [](const std::string& msg) { perfLog("[Lua] %s\n", msg.c_str()); });
    lua.set_function("dB", [](float db) -> float { return std::pow(10.0f, db / 20.0f); });
}

void LuaEngine::executeActionCode(const std::string& code,
                                    const std::vector<ActionAlgebra::Value>& args,
                                    float midiValue) {
    // Build a 1-based Lua table for args and set as global.
    sol::table argsTable = lua.create_table();
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& v = args[i];
        switch (v.kind) {
            case ActionAlgebra::Value::Kind::Number:
                argsTable[i + 1] = v.number;
                break;
            case ActionAlgebra::Value::Kind::Text:
                argsTable[i + 1] = v.text;
                break;
            default:
                argsTable[i + 1] = sol::nil;
                break;
        }
    }
    lua["args"]  = argsTable;
    lua["value"] = (double)midiValue;

    auto result = lua.safe_script(code, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        perfLog("[LuaAction] error: %s\n", err.what());
    }
}

std::string LuaEngine::executeString(const std::string& code) {
    auto result = lua.safe_script(code, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        return std::string("error: ") + err.what();
    }
    if (result.get_type() != sol::type::none && result.get_type() != sol::type::nil) {
        sol::object obj = result;
        if (obj.is<std::string>()) return obj.as<std::string>();
        if (obj.is<double>()) return std::to_string(obj.as<double>());
        if (obj.is<bool>()) return obj.as<bool>() ? "true" : "false";
        if (obj.is<sol::table>()) {
            auto tbl = obj.as<sol::table>();
            std::string s = "[";
            bool first = true;
            for (auto& pair : tbl) {
                if (!first) s += ", ";
                auto& val = pair.second;
                if (val.is<std::string>()) s += "\"" + val.as<std::string>() + "\"";
                else if (val.is<double>()) s += std::to_string(val.as<double>());
                else if (val.is<bool>()) s += val.as<bool>() ? "true" : "false";
                else if (val.is<sol::table>()) {
                    auto inner = val.as<sol::table>();
                    s += "{";
                    bool ifirst = true;
                    for (auto& ip : inner) {
                        if (!ifirst) s += ", ";
                        s += "\"" + ip.first.as<std::string>() + "\": ";
                        auto& iv = ip.second;
                        if (iv.is<std::string>()) s += "\"" + iv.as<std::string>() + "\"";
                        else if (iv.is<double>()) s += std::to_string(iv.as<double>());
                        else s += "\"?\"";
                        ifirst = false;
                    }
                    s += "}";
                }
                first = false;
            }
            s += "]";
            return s;
        }
    }
    return "ok";
}

bool LuaEngine::loadSong(const std::string& path) {
    unloadSong();
    perfLog("[Lua] Loading song: %s\n", path.c_str());
    auto result = lua.safe_script_file(path, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        perfLog("[Lua] ERROR: %s\n", err.what());
        return false;
    }
    perfLog("[Lua] Song loaded successfully\n");
    return true;
}

void LuaEngine::unloadSong() {
    coordRef.cancelAllAutomation();
    coordRef.unloadSong();
}

std::vector<std::string> LuaEngine::listSongs() const {
    std::vector<std::string> songs;
    auto dir = getSongsDirectory();
    if (!fs::exists(dir)) return songs;
    for (auto& entry : fs::directory_iterator(dir))
        if (entry.path().extension() == ".lua")
            songs.push_back(entry.path().stem().string());
    std::sort(songs.begin(), songs.end());
    return songs;
}

std::string LuaEngine::getSongsDirectory() {
    auto home = std::string(getenv("HOME"));
    return home + "/.config/performance/songs";
}
