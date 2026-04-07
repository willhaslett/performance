#include "scripting/LuaEngine.h"
#include "api/PerformanceAPI.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "automation/AutomationEngine.h"
#include "engine/Log.h"
#include "song/Song.h"
#include <juce_events/juce_events.h>
#include <filesystem>

namespace fs = std::filesystem;

static std::string getLuaLibDirectory() {
    auto home = std::string(getenv("HOME"));
    return home + "/.config/performance/lua_lib";
}

LuaEngine::LuaEngine(PerformanceAPI& api) : legacyApi(&api) {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    registerAPI();
    loadLibraries();
}

LuaEngine::LuaEngine(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator)
    : statePtr(&state), enginePtr(&engine), coordPtr(&coordinator) {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    registerNewAPI();
    loadLibraries();
}

void LuaEngine::loadLibraries() {
    auto libDir = getLuaLibDirectory();
    if (!fs::exists(libDir)) return;

    for (auto& entry : fs::directory_iterator(libDir)) {
        if (entry.path().extension() == ".lua") {
            auto result = lua.safe_script_file(entry.path().string(), sol::script_pass_on_error);
            if (result.valid()) {
                perfLog("[Lua] Loaded library: %s\n", entry.path().filename().c_str());
            } else {
                sol::error err = result;
                perfLog("[Lua] ERROR loading library %s: %s\n",
                        entry.path().filename().c_str(), err.what());
            }
        }
    }
}

void LuaEngine::registerAPI() {
    // Song metadata — creates the song in the registry
    lua.set_function("song", [this](const std::string& name) {
        legacyApi->createSong(juce::String(name));
    });

    // Name-to-ID resolution helpers for Lua convenience
    // Lua users pass display names; these resolve to UUIDs for the API.
    auto resolveTrackId = [this](const std::string& name) -> juce::String {
        auto id = legacyApi->findTrackIdByName(juce::String(name));
        return id.empty() ? juce::String(name) : juce::String(id);
    };
    auto resolveBusId = [this](const std::string& name) -> juce::String {
        auto id = legacyApi->findBusIdByName(juce::String(name));
        return id.empty() ? juce::String(name) : juce::String(id);
    };
    // Resolve parent (track or bus) by name
    auto resolveParentId = [this](const std::string& name) -> juce::String {
        if (name == "Output") return legacyApi->getMasterOutputId();
        auto id = legacyApi->findTrackIdByName(juce::String(name));
        if (!id.empty()) return juce::String(id);
        id = legacyApi->findBusIdByName(juce::String(name));
        if (!id.empty()) return juce::String(id);
        return juce::String(name);
    };

    // Track management
    lua.set_function("createTrack", [this](const std::string& name) -> std::string {
        return legacyApi->createTrack(juce::String(name)).toStdString();
    });
    lua.set_function("removeTrack", [this, resolveTrackId](const std::string& name) {
        legacyApi->removeTrack(resolveTrackId(name));
    });
    lua.set_function("addInstrument", [this, resolveTrackId](const std::string& track, const std::string& plugin,
                                              sol::optional<std::string> preset) {
        legacyApi->addInstrument(resolveTrackId(track), juce::String(plugin),
                          juce::String(preset.value_or("")));
    });
    lua.set_function("addTrackEffect", [this, resolveParentId](const std::string& parent, const std::string& effect,
                                               const std::string& plugin) {
        legacyApi->addEffect(resolveParentId(parent), juce::String(effect), juce::String(plugin));
    });
    lua.set_function("addEffect", [this, resolveParentId](const std::string& parent, const std::string& effect,
                                          const std::string& plugin) {
        legacyApi->addEffect(resolveParentId(parent), juce::String(effect), juce::String(plugin));
    });
    lua.set_function("setTrackMidiEnabled", [this, resolveTrackId](const std::string& track, bool enabled) {
        legacyApi->setTrackMidiEnabled(resolveTrackId(track), enabled);
    });
    lua.set_function("setTrackGain", [this, resolveTrackId](const std::string& track, float gain) {
        legacyApi->setTrackGain(resolveTrackId(track), gain);
    });

    // Bus management
    lua.set_function("createBus", [this](const std::string& name) -> std::string {
        return legacyApi->createBus(juce::String(name)).toStdString();
    });
    lua.set_function("removeBus", [this, resolveBusId](const std::string& name) {
        legacyApi->removeBus(resolveBusId(name));
    });
    lua.set_function("addBusEffect", [this, resolveParentId](const std::string& parent, const std::string& effect,
                                             const std::string& plugin) {
        legacyApi->addEffect(resolveParentId(parent), juce::String(effect), juce::String(plugin));
    });
    lua.set_function("removeEffect", [this, resolveParentId](const std::string& parent, const std::string& effect) {
        legacyApi->removeEffect(resolveParentId(parent), juce::String(effect));
    });
    lua.set_function("setBusGain", [this, resolveBusId](const std::string& bus, float gain) {
        legacyApi->setBusGain(resolveBusId(bus), gain);
    });

    // Sends
    lua.set_function("addSend", [this, resolveTrackId, resolveBusId](const std::string& track, const std::string& bus,
                                        sol::optional<float> gain) {
        legacyApi->addSend(resolveTrackId(track), resolveBusId(bus), gain.value_or(1.0f));
    });
    lua.set_function("setSendGain", [this, resolveTrackId, resolveBusId](const std::string& track, const std::string& bus, float gain) {
        legacyApi->setSendGain(resolveTrackId(track), resolveBusId(bus), gain);
    });

    // Parameters
    lua.set_function("setParam", [this, resolveTrackId](const std::string& track, const std::string& param, float value) {
        legacyApi->setParam(resolveTrackId(track), juce::String(param), value);
    });
    lua.set_function("setEffectParam", [this, resolveParentId](const std::string& parent, const std::string& effect,
                                               const std::string& param, float value) {
        legacyApi->setEffectParam(resolveParentId(parent), juce::String(effect), juce::String(param), value);
    });
    lua.set_function("getParam", [this, resolveTrackId](const std::string& track, const std::string& param) -> float {
        return legacyApi->getParam(resolveTrackId(track), juce::String(param));
    });
    lua.set_function("getEffectParam", [this, resolveParentId](const std::string& parent, const std::string& effect,
                                               const std::string& param) -> float {
        return legacyApi->getEffectParam(resolveParentId(parent), juce::String(effect), juce::String(param));
    });

    // MIDI control binding
    // bind(controlType, channel, number, actionName, args, description)
    // args is a Lua table that gets serialized to JSON
    lua.set_function("bind", [this](const std::string& type, int channel, int number,
                                     const std::string& actionName,
                                     sol::optional<sol::object> argsOrDesc,
                                     sol::optional<std::string> descOpt) {
        // Handle optional args: bind("cc", 10, 20, "action", {args}, "desc")
        //                    or bind("cc", 10, 20, "action", "desc")
        std::string argsJson = "[]";
        std::string description;

        if (argsOrDesc.has_value()) {
            auto& val = argsOrDesc.value();
            if (val.is<sol::table>()) {
                // It's an args table — serialize to JSON array
                auto tbl = val.as<sol::table>();
                juce::Array<juce::var> arr;
                for (size_t i = 1; i <= tbl.size(); ++i) {
                    sol::object item = tbl[i];
                    if (item.is<std::string>())
                        arr.add(juce::var(juce::String(item.as<std::string>())));
                    else if (item.is<double>())
                        arr.add(juce::var(item.as<double>()));
                    else if (item.is<bool>())
                        arr.add(juce::var(item.as<bool>()));
                }
                argsJson = juce::JSON::toString(juce::var(arr)).toStdString();
            } else if (val.is<std::string>()) {
                // It's the description (no args)
                description = val.as<std::string>();
            }
        }
        if (descOpt.has_value())
            description = descOpt.value();

        legacyApi->bind(juce::String(type), channel, number,
                 juce::String(actionName), juce::String(argsJson),
                 juce::String(description));
    });
    lua.set_function("unbind", [this](const std::string& type, int channel, int number) {
        legacyApi->unbind(juce::String(type), channel, number);
    });

    // Automation
    lua.set_function("interpolate", [this](float from, float to, float duration,
                                            sol::function callback,
                                            sol::optional<sol::object> easing) -> int {
        auto luaCallback = callback;
        PerformanceAPI::EasingFn easingFn = nullptr;

        if (easing.has_value()) {
            auto& val = easing.value();
            if (val.is<std::string>()) {
                auto name = val.as<std::string>();
                easingFn = AutomationEngine::easingByName(name);
            } else if (val.is<sol::function>()) {
                auto luaEasing = val.as<sol::function>();
                easingFn = [luaEasing](float t) mutable -> float {
                    return luaEasing(t);
                };
            }
        }

        return legacyApi->interpolate(from, to, duration,
                               [luaCallback](float value) mutable { luaCallback(value); },
                               std::move(easingFn));
    });

    lua.set_function("delay", [this](float seconds, sol::function callback) -> int {
        auto luaCallback = callback;
        return legacyApi->delay(seconds, [luaCallback]() mutable { luaCallback(); });
    });

    lua.set_function("cancel", [this](int handle) {
        legacyApi->cancelAutomation(handle);
    });
    lua.set_function("cancelAll", [this]() {
        legacyApi->cancelAllAutomation();
    });

    // Track gain query (needed by automation helpers)
    lua.set_function("getTrackGain", [this, resolveTrackId](const std::string& track) -> float {
        return legacyApi->getTrackGain(resolveTrackId(track));
    });

    // Presets (plugin state)
    lua.set_function("savePreset", [this, resolveTrackId](const std::string& track, const std::string& name) {
        legacyApi->savePreset(resolveTrackId(track), juce::String(name));
    });
    lua.set_function("loadPreset", [this, resolveTrackId](const std::string& track, const std::string& name) {
        legacyApi->loadPreset(resolveTrackId(track), juce::String(name));
    });
    lua.set_function("listPresets", [this](const std::string& plugin) -> sol::table {
        auto names = legacyApi->listPresets(juce::String(plugin));
        sol::table result = lua.create_table();
        for (size_t i = 0; i < names.size(); ++i)
            result[i + 1] = names[i].toStdString();
        return result;
    });

    // Plugin UI
    lua.set_function("openEditor", [this, resolveParentId](const std::string& parent, sol::optional<std::string> effect) {
        legacyApi->openPluginEditor(resolveParentId(parent), juce::String(effect.value_or("")));
    });

    // Query
    lua.set_function("listPlugins", [this]() -> sol::table {
        auto plugins = legacyApi->listPlugins();
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
        // Defer to next message loop iteration — this may be called from
        // inside a MIDI handler, and unloadSong clears the dispatch map
        // we're currently iterating.
        auto path = getSongsDirectory() + "/" + name + ".lua";
        juce::MessageManager::callAsync([this, path] {
            loadSong(path);
        });
    });
    lua.set_function("unloadSong", [this]() {
        juce::MessageManager::callAsync([this] {
            unloadSong();
        });
    });
    lua.set_function("saveInitialState", [this]() {
        legacyApi->saveInitialState();
    });
    lua.set_function("loadInitialState", [this]() {
        legacyApi->loadInitialState();
    });
    lua.set_function("saveScore", [this](const std::string& scoreJson) {
        legacyApi->saveScore(juce::String(scoreJson));
    });
    lua.set_function("getScore", [this]() -> std::string {
        return legacyApi->getScore().toStdString();
    });
    lua.set_function("replayScore", [this](sol::optional<int> upToStep) {
        legacyApi->replayScore(upToStep.value_or(-1));
    });

    // Generic Registry CRUD
    lua.set_function("registryCreate", [this](const std::string& type, sol::table fields) -> std::string {
        std::map<std::string, std::string> f;
        for (auto& [key, val] : fields)
            f[key.as<std::string>()] = val.as<std::string>();
        return legacyApi->registryCreate(type, f);
    });
    lua.set_function("registryGet", [this](const std::string& id) -> sol::table {
        auto fields = legacyApi->registryGet(id);
        sol::table result = lua.create_table();
        for (auto& [k, v] : fields)
            result[k] = v;
        return result;
    });
    lua.set_function("registryList", [this](const std::string& type,
                                             sol::optional<sol::table> filters) -> sol::table {
        std::map<std::string, std::string> f;
        if (filters.has_value()) {
            for (auto& [key, val] : filters.value())
                f[key.as<std::string>()] = val.as<std::string>();
        }
        auto entities = legacyApi->registryList(type, f);
        sol::table result = lua.create_table();
        for (size_t i = 0; i < entities.size(); ++i) {
            sol::table row = lua.create_table();
            for (auto& [k, v] : entities[i])
                row[k] = v;
            result[i + 1] = row;
        }
        return result;
    });
    lua.set_function("registryUpdate", [this](const std::string& id, sol::table fields) {
        std::map<std::string, std::string> f;
        for (auto& [key, val] : fields)
            f[key.as<std::string>()] = val.as<std::string>();
        legacyApi->registryUpdate(id, f);
    });
    lua.set_function("registryDelete", [this](const std::string& id) {
        legacyApi->registryDelete(id);
    });

    // Utility
    lua.set_function("log", [this](const std::string& msg) {
        legacyApi->log(juce::String(msg));
    });
    lua.set_function("dB", [](float db) -> float {
        return std::pow(10.0f, db / 20.0f);
    });
}

std::string LuaEngine::executeString(const std::string& code) {
    auto result = lua.safe_script(code, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        return std::string("error: ") + err.what();
    }
    // If the script returned a value, convert to string
    if (result.get_type() != sol::type::none && result.get_type() != sol::type::nil) {
        sol::object obj = result;
        if (obj.is<std::string>())
            return obj.as<std::string>();
        if (obj.is<double>())
            return std::to_string(obj.as<double>());
        if (obj.is<bool>())
            return obj.as<bool>() ? "true" : "false";
        if (obj.is<sol::table>()) {
            // Serialize table to JSON-ish string
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
                        auto key = ip.first.as<std::string>();
                        auto& iv = ip.second;
                        s += "\"" + key + "\": ";
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
    if (coordPtr) {
        coordPtr->cancelAllAutomation();
        coordPtr->unloadSong();
    } else if (legacyApi) {
        legacyApi->cancelAllAutomation();
        legacyApi->unbindAll();
        legacyApi->unloadSong();
    }
}

std::vector<std::string> LuaEngine::listSongs() const {
    std::vector<std::string> songs;
    auto dir = getSongsDirectory();

    if (!fs::exists(dir)) return songs;

    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".lua")
            songs.push_back(entry.path().stem().string());
    }

    std::sort(songs.begin(), songs.end());
    return songs;
}

std::string LuaEngine::getSongsDirectory() {
    auto home = std::string(getenv("HOME"));
    return home + "/.config/performance/songs";
}

// ============================================================================
// New API registration (StateAPI + EngineAPI + PerformanceCoordinator)
// ============================================================================

void LuaEngine::registerNewAPI() {
    auto& state = *statePtr;
    auto& engine = *enginePtr;
    auto& coord = *coordPtr;

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
    lua.set_function("removeEffect", [&state](const std::string& parent, const std::string& effectId) {
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

    // Automation (coordinator)
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

    // Song management (coordinator)
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
    lua.set_function("saveInitialState", [&coord]() { coord.saveInitialState(); });
    lua.set_function("loadInitialState", [&coord]() { coord.loadInitialState(); });

    // MIDI control binding (state)
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
        auto songId = state.getMasterOutputId();  // current song ID
        if (!songId.empty())
            state.addBinding(songId, type, channel, number, action->id, argsJson, description);
    });
    lua.set_function("unbind", [](const std::string&, int, int) {
        // TODO: find binding by control and remove
    });

    // Generic state CRUD
    lua.set_function("registryCreate", [this, &state](const std::string& type, sol::table fields) -> std::string {
        // Map to typed state operations
        if (type == "track") {
            auto name = fields.get_or<std::string>("name", "Track");
            return state.createTrack(name);
        } else if (type == "bus") {
            auto name = fields.get_or<std::string>("name", "Bus");
            return state.createBus(name);
        }
        return "";
    });
    lua.set_function("registryList", [this, &state](const std::string& type,
                                                     sol::optional<sol::table>) -> sol::table {
        sol::table result = lua.create_table();
        if (type == "track") {
            auto tracks = state.listTracks();
            for (size_t i = 0; i < tracks.size(); ++i) {
                sol::table row = lua.create_table();
                row["id"] = tracks[i].id;
                row["name"] = tracks[i].name;
                result[i + 1] = row;
            }
        } else if (type == "bus") {
            auto busses = state.listBusses();
            for (size_t i = 0; i < busses.size(); ++i) {
                sol::table row = lua.create_table();
                row["id"] = busses[i].id;
                row["name"] = busses[i].name;
                result[i + 1] = row;
            }
        } else if (type == "song") {
            auto& songs = state.allSongs();
            for (size_t i = 0; i < songs.size(); ++i) {
                sol::table row = lua.create_table();
                row["id"] = songs[i].id;
                row["name"] = songs[i].name;
                result[i + 1] = row;
            }
        }
        return result;
    });
    lua.set_function("registryDelete", [&state](const std::string& id) {
        // Try to figure out what it is and delete
        if (state.findTrack(id)) state.removeTrack(id);
        else if (state.findBus(id)) state.removeBus(id);
        else if (state.findSong(id)) state.deleteSong(id);
    });

    // Utility
    lua.set_function("log", [](const std::string& msg) {
        perfLog("[Lua] %s\n", msg.c_str());
    });
    lua.set_function("dB", [](float db) -> float {
        return std::pow(10.0f, db / 20.0f);
    });
}
