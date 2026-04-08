#pragma once
#include "state/StateModel.h"
#include <string>
#include <sqlite3.h>

class StateAPI;

// SQLite persistence — load on startup, save on demand.
// Not in the hot path. The in-memory StateAPI is the runtime SSOT.

class PersistenceLayer {
public:
    PersistenceLayer();
    ~PersistenceLayer();

    void open(const std::string& dbPath);
    void close();

    void loadInto(StateAPI& state);
    void saveFrom(const StateAPI& state);

private:
    sqlite3* db = nullptr;

    void createSchema();
    void exec(const std::string& sql);
    sqlite3_stmt* prepare(const std::string& sql);

    // Load helpers — build AppState directly, no StateAPI mutators
    void readPlugins(AppState& out);
    void readPresets(AppState& out);
    void readActions(AppState& out);
    void readSongs(AppState& out);
    void readConfig(AppState& out);

    // Save helpers
    void savePlugins(const StateAPI& state);
    void savePresets(const StateAPI& state);
    void saveActions(const StateAPI& state);
    void saveSongs(const StateAPI& state);
    void saveConfig(const StateAPI& state);
};
