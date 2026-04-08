#pragma once
#include <sol/sol.hpp>
#include <string>
#include <vector>

class StateAPI;
class EngineAPI;
class PerformanceCoordinator;

class LuaEngine {
public:
    LuaEngine(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator);

    // Execute a Lua string, return result or error
    std::string executeString(const std::string& code);

    // Load and execute a .lua song file. Unloads current song first.
    bool loadSong(const std::string& path);

    // Unload current song (clears tracks, busses, bindings)
    void unloadSong();

    // List available .lua songs in the songs directory
    std::vector<std::string> listSongs() const;

    // Songs directory path
    static std::string getSongsDirectory();

private:
    StateAPI& stateRef;
    EngineAPI& engineRef;
    PerformanceCoordinator& coordRef;
    sol::state lua;

    void registerAPI();
    void loadLibraries();
};
