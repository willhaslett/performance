#include "scripting/LuaEngine.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "automation/AutomationEngine.h"
#include "engine/Log.h"
#include <juce_events/juce_events.h>
#include <filesystem>

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

    // Name resolution helpers
    auto resolveTrackId = [&state](const std::string& name) -> std::string {
        auto id = state.findTrackIdByName(name);
        return id.empty() ? name : id;
    };
    auto resolveBusId = [&state](const std::string& name) -> std::string {
        auto id = state.findBusIdByName(name);
        return id.empty() ? name : id;
    };
    auto resolveParentId = [&state](const std::string& name) -> std::string {
        if (name == "Output") return state.getMasterOutputId();
        auto id = state.findTrackIdByName(name);
        if (!id.empty()) return id;
        id = state.findBusIdByName(name);
        if (!id.empty()) return id;
        return name;
    };

    // Song
    lua.set_function("song", [&coord](const std::string& name) {
        coord.createSong(juce::String(name));
    });

    // Track management
    lua.set_function("createTrack", [&state](const std::string& name) -> std::string {
        return state.createTrack(name);
    });
    lua.set_function("createAudioInputTrack", [&state](const std::string& name,
                                                          int inputStart, int inputCount) -> std::string {
        return state.createAudioInputTrack(name, inputStart, inputCount);
    });
    lua.set_function("setTrackInputChannels", [&state, resolveTrackId](const std::string& track,
                                                int start, int count) {
        state.setTrackInputChannels(resolveTrackId(track), start, count);
    });
    lua.set_function("listInputChannels", [this, &engine]() -> sol::table {
        auto names = engine.getInputChannelNames();
        sol::table result = lua.create_table();
        for (size_t i = 0; i < names.size(); ++i)
            result[i + 1] = names[i].toStdString();
        return result;
    });
    lua.set_function("removeTrack", [&state, resolveTrackId](const std::string& name) {
        state.removeTrack(resolveTrackId(name));
    });
    lua.set_function("addInstrument", [&state, resolveTrackId](const std::string& track,
                                       const std::string& plugin, sol::optional<std::string> preset) {
        auto trackId = resolveTrackId(track);
        auto* p = state.findPluginByName(plugin);
        if (!p) return;
        std::string presetId;
        if (preset.has_value() && !preset.value().empty()) {
            auto* pr = state.findPreset(p->id, preset.value());
            if (pr) presetId = pr->id;
        }
        state.setTrackPlugin(trackId, p->id, presetId);
    });
    lua.set_function("addEffect", [&state, resolveParentId](const std::string& parent,
                                    const std::string& effectName, const std::string& plugin) {
        auto* p = state.findPluginByName(plugin);
        if (p) state.addEffect(resolveParentId(parent), effectName, p->id);
    });
    lua.set_function("addTrackEffect", [&state, resolveParentId](const std::string& parent,
                                         const std::string& effectName, const std::string& plugin) {
        auto* p = state.findPluginByName(plugin);
        if (p) state.addEffect(resolveParentId(parent), effectName, p->id);
    });
    lua.set_function("addBusEffect", [&state, resolveParentId](const std::string& parent,
                                       const std::string& effectName, const std::string& plugin) {
        auto* p = state.findPluginByName(plugin);
        if (p) state.addEffect(resolveParentId(parent), effectName, p->id);
    });
    lua.set_function("removeEffect", [&state](const std::string&, const std::string& effectId) {
        state.removeEffect(effectId);
    });
    lua.set_function("setTrackMidiEnabled", [&state, resolveTrackId](const std::string& track, bool enabled) {
        state.setTrackMidiEnabled(resolveTrackId(track), enabled);
    });
    lua.set_function("setTrackGain", [&state, resolveTrackId](const std::string& track, float gain) {
        state.setTrackGain(resolveTrackId(track), gain);
    });
    lua.set_function("getTrackGain", [&state, resolveTrackId](const std::string& track) -> float {
        return state.getTrackGain(resolveTrackId(track));
    });

    // Bus management
    lua.set_function("createBus", [&state](const std::string& name) -> std::string {
        return state.createBus(name);
    });
    lua.set_function("removeBus", [&state, resolveBusId](const std::string& name) {
        state.removeBus(resolveBusId(name));
    });
    lua.set_function("setBusGain", [&state, resolveBusId](const std::string& bus, float gain) {
        state.setBusGain(resolveBusId(bus), gain);
    });

    // Sends
    lua.set_function("addSend", [&state, resolveTrackId, resolveBusId](const std::string& track,
                                  const std::string& bus, sol::optional<float> gain) {
        state.addSend(resolveTrackId(track), resolveBusId(bus), gain.value_or(1.0f));
    });
    lua.set_function("setSendGain", [&state, resolveTrackId, resolveBusId](const std::string& track,
                                      const std::string& bus, float gain) {
        state.setSendGainByBus(resolveTrackId(track), resolveBusId(bus), gain);
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
    lua.set_function("saveInitialState", [&coord]() { coord.saveInitialState(); });
    lua.set_function("loadInitialState", [&coord]() { coord.loadInitialState(); });

    // Devices
    lua.set_function("registerDevice", [&state, &coord](const std::string& name, const std::string& portName) -> std::string {
        auto id = state.registerDevice(name, portName);
        coord.refreshMidiDevices();
        return id;
    });
    lua.set_function("addDeviceControl", [&state](const std::string& deviceId, const std::string& name,
                                                    const std::string& controlType, int channel, int number,
                                                    sol::optional<std::string> group) {
        state.addDeviceControl(deviceId, name, controlType, channel, number, group.value_or(""));
    });
    lua.set_function("addDeviceToSong", [&state](const std::string& songId, const std::string& deviceId) {
        state.addDeviceToSong(songId, deviceId);
    });
    lua.set_function("listDevices", [this, &state]() -> sol::table {
        sol::table result = lua.create_table();
        for (auto& d : state.allDevices()) {
            sol::table row = lua.create_table();
            row["id"] = d.id;
            row["name"] = d.name;
            row["port"] = d.midiPortName;
            result.add(row);
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
                                             sol::optional<std::string> descOpt) {
        std::string argsJson = "[]";
        std::string description;
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

        auto* action = state.findActionByName(actionName);
        if (!action) return;
        auto songId = state.getMasterOutputId();
        if (!songId.empty())
            state.addBinding(songId, type, channel, number, action->id, argsJson, description);
    });

    // Generic state queries
    lua.set_function("registryList", [this, &state](const std::string& type,
                                                     sol::optional<sol::table>) -> sol::table {
        sol::table result = lua.create_table();
        if (type == "track") {
            for (auto& t : state.listTracks()) {
                sol::table row = lua.create_table();
                row["id"] = t.id; row["name"] = t.name;
                result.add(row);
            }
        } else if (type == "bus") {
            for (auto& b : state.listBusses()) {
                sol::table row = lua.create_table();
                row["id"] = b.id; row["name"] = b.name;
                result.add(row);
            }
        } else if (type == "song") {
            for (auto& s : state.allSongs()) {
                sol::table row = lua.create_table();
                row["id"] = s.id; row["name"] = s.name;
                result.add(row);
            }
        }
        return result;
    });
    lua.set_function("registryDelete", [&state](const std::string& id) {
        if (state.findTrack(id)) state.removeTrack(id);
        else if (state.findBus(id)) state.removeBus(id);
        else if (state.findSong(id)) state.deleteSong(id);
    });

    // Utility
    lua.set_function("log", [](const std::string& msg) { perfLog("[Lua] %s\n", msg.c_str()); });
    lua.set_function("dB", [](float db) -> float { return std::pow(10.0f, db / 20.0f); });
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
