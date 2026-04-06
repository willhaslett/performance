#include "scripting/LuaEngine.h"
#include "api/PerformanceAPI.h"
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

LuaEngine::LuaEngine(PerformanceAPI& api) : api(api) {
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
        api.createSong(juce::String(name));
    });

    // Track management
    lua.set_function("createTrack", [this](const std::string& name) {
        api.createTrack(juce::String(name));
    });
    lua.set_function("removeTrack", [this](const std::string& name) {
        api.removeTrack(juce::String(name));
    });
    lua.set_function("addInstrument", [this](const std::string& track, const std::string& plugin,
                                              sol::optional<std::string> snapshot) {
        api.addInstrument(juce::String(track), juce::String(plugin),
                          juce::String(snapshot.value_or("")));
    });
    lua.set_function("addTrackEffect", [this](const std::string& track, const std::string& effect,
                                               const std::string& plugin) {
        api.addTrackEffect(juce::String(track), juce::String(effect), juce::String(plugin));
    });
    lua.set_function("setTrackMidiEnabled", [this](const std::string& track, bool enabled) {
        api.setTrackMidiEnabled(juce::String(track), enabled);
    });
    lua.set_function("setTrackGain", [this](const std::string& track, float gain) {
        api.setTrackGain(juce::String(track), gain);
    });

    // Bus management
    lua.set_function("createBus", [this](const std::string& name) {
        api.createBus(juce::String(name));
    });
    lua.set_function("removeBus", [this](const std::string& name) {
        api.removeBus(juce::String(name));
    });
    lua.set_function("addBusEffect", [this](const std::string& bus, const std::string& effect,
                                             const std::string& plugin) {
        api.addBusEffect(juce::String(bus), juce::String(effect), juce::String(plugin));
    });
    lua.set_function("setBusGain", [this](const std::string& bus, float gain) {
        api.setBusGain(juce::String(bus), gain);
    });

    // Sends
    lua.set_function("addSend", [this](const std::string& track, const std::string& bus,
                                        sol::optional<float> gain) {
        api.addSend(juce::String(track), juce::String(bus), gain.value_or(1.0f));
    });
    lua.set_function("setSendGain", [this](const std::string& track, const std::string& bus, float gain) {
        api.setSendGain(juce::String(track), juce::String(bus), gain);
    });

    // Parameters
    lua.set_function("setParam", [this](const std::string& track, const std::string& param, float value) {
        api.setParam(juce::String(track), juce::String(param), value);
    });
    lua.set_function("setEffectParam", [this](const std::string& track, const std::string& effect,
                                               const std::string& param, float value) {
        api.setEffectParam(juce::String(track), juce::String(effect), juce::String(param), value);
    });
    lua.set_function("getParam", [this](const std::string& track, const std::string& param) -> float {
        return api.getParam(juce::String(track), juce::String(param));
    });
    lua.set_function("getEffectParam", [this](const std::string& track, const std::string& effect,
                                               const std::string& param) -> float {
        return api.getEffectParam(juce::String(track), juce::String(effect), juce::String(param));
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

        api.bind(juce::String(type), channel, number,
                 juce::String(actionName), juce::String(argsJson),
                 juce::String(description));
    });
    lua.set_function("unbind", [this](const std::string& type, int channel, int number) {
        api.unbind(juce::String(type), channel, number);
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

        return api.interpolate(from, to, duration,
                               [luaCallback](float value) mutable { luaCallback(value); },
                               std::move(easingFn));
    });

    lua.set_function("delay", [this](float seconds, sol::function callback) -> int {
        auto luaCallback = callback;
        return api.delay(seconds, [luaCallback]() mutable { luaCallback(); });
    });

    lua.set_function("cancel", [this](int handle) {
        api.cancelAutomation(handle);
    });
    lua.set_function("cancelAll", [this]() {
        api.cancelAllAutomation();
    });

    // Track gain query (needed by automation helpers)
    lua.set_function("getTrackGain", [this](const std::string& track) -> float {
        return api.getTrackGain(juce::String(track));
    });

    // Presets
    lua.set_function("listPresets", [this](const std::string& track) -> sol::table {
        auto presets = api.listPresets(juce::String(track));
        sol::table result = lua.create_table();
        for (size_t i = 0; i < presets.size(); ++i)
            result[i + 1] = presets[i].toStdString();
        return result;
    });
    lua.set_function("loadPreset", [this](const std::string& track, sol::object arg) {
        if (arg.is<int>()) {
            api.loadPreset(juce::String(track), arg.as<int>());
        } else if (arg.is<std::string>()) {
            api.loadPresetByName(juce::String(track), juce::String(arg.as<std::string>()));
        }
    });

    // Plugin state snapshots
    lua.set_function("saveSnapshot", [this](const std::string& track, const std::string& name) {
        api.saveSnapshot(juce::String(track), juce::String(name));
    });
    lua.set_function("loadSnapshot", [this](const std::string& track, const std::string& name) {
        api.loadSnapshot(juce::String(track), juce::String(name));
    });
    lua.set_function("listSnapshots", [this](const std::string& plugin) -> sol::table {
        auto names = api.listSnapshots(juce::String(plugin));
        sol::table result = lua.create_table();
        for (size_t i = 0; i < names.size(); ++i)
            result[i + 1] = names[i].toStdString();
        return result;
    });

    // Plugin UI
    lua.set_function("openEditor", [this](const std::string& track, sol::optional<std::string> effect) {
        api.openPluginEditor(juce::String(track), juce::String(effect.value_or("")));
    });

    // Query
    lua.set_function("listPlugins", [this]() -> sol::table {
        auto plugins = api.listPlugins();
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
        api.saveInitialState();
    });
    lua.set_function("loadInitialState", [this]() {
        api.loadInitialState();
    });
    lua.set_function("saveScore", [this](const std::string& scoreJson) {
        api.saveScore(juce::String(scoreJson));
    });
    lua.set_function("getScore", [this]() -> std::string {
        return api.getScore().toStdString();
    });
    lua.set_function("replayScore", [this](sol::optional<int> upToStep) {
        api.replayScore(upToStep.value_or(-1));
    });

    // Generic Registry CRUD
    lua.set_function("registryCreate", [this](const std::string& type, sol::table fields) -> std::string {
        std::map<std::string, std::string> f;
        for (auto& [key, val] : fields)
            f[key.as<std::string>()] = val.as<std::string>();
        return api.registryCreate(type, f);
    });
    lua.set_function("registryGet", [this](const std::string& id) -> sol::table {
        auto fields = api.registryGet(id);
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
        auto entities = api.registryList(type, f);
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
        api.registryUpdate(id, f);
    });
    lua.set_function("registryDelete", [this](const std::string& id) {
        api.registryDelete(id);
    });

    // Utility
    lua.set_function("log", [this](const std::string& msg) {
        api.log(juce::String(msg));
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
    api.cancelAllAutomation();
    api.unbindAll();
    api.unloadSong();
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
