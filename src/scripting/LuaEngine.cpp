#include "scripting/LuaEngine.h"
#include "api/PerformanceAPI.h"
#include "automation/AutomationEngine.h"
#include "engine/Log.h"
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
    // Song metadata
    lua.set_function("song", [this](const std::string& name) {
        api.log("Song: " + juce::String(name));
    });

    // Track management
    lua.set_function("createTrack", [this](const std::string& name) {
        api.createTrack(juce::String(name));
    });
    lua.set_function("removeTrack", [this](const std::string& name) {
        api.removeTrack(juce::String(name));
    });
    lua.set_function("addInstrument", [this](const std::string& track, const std::string& plugin) {
        api.addInstrument(juce::String(track), juce::String(plugin));
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
    lua.set_function("bind", [this](const std::string& type, int channel, int number,
                                     sol::function handler, sol::optional<std::string> description) {
        // Capture the Lua function by value (shared ownership via sol::function)
        auto luaHandler = handler;
        api.bind(juce::String(type), channel, number,
                 [luaHandler](float value) mutable {
                     luaHandler(value);
                 },
                 juce::String(description.value_or("")));
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

    // Utility
    lua.set_function("log", [this](const std::string& msg) {
        api.log(juce::String(msg));
    });
    lua.set_function("dB", [](float db) -> float {
        return std::pow(10.0f, db / 20.0f);
    });
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
