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
    // Returns true on a successful commit, false if anything goes wrong
    // (BEGIN / COMMIT failure, etc.). On failure, the transaction has been
    // rolled back and the DB is unchanged from before saveFrom was called.
    // Details are logged.
    bool saveFrom(const StateAPI& state);

private:
    sqlite3* db = nullptr;
    std::string dbPath;
    // Tripped by stepWrite() on any SQLITE error during a save transaction.
    // saveFrom() checks this before COMMIT — any single write failure ->
    // ROLLBACK instead of committing partial / broken data.
    bool saveHadError = false;

    void createSchema();
    // Returns true on SQLITE_OK. Errors are logged.
    bool exec(const std::string& sql);
    sqlite3_stmt* prepare(const std::string& sql);
    // Step + finalize a write statement. Returns true if step returned
    // SQLITE_DONE or SQLITE_ROW. On failure, logs with the given context.
    // Always finalizes the statement, regardless of step outcome.
    bool stepWrite(sqlite3_stmt* stmt, const char* context);

    // Load helpers — build AppState directly, no StateAPI mutators
    void readPlugins(AppState& out);
    void readPresets(AppState& out);
    void readActions(AppState& out);
    void readSongs(AppState& out);
    void readDevices(AppState& out);
    void readConfig(AppState& out);

    // Save helpers
    void savePlugins(const StateAPI& state);
    void savePresets(const StateAPI& state);
    void saveActions(const StateAPI& state);
    void saveSongs(const StateAPI& state);
    void saveConfig(const StateAPI& state);
};
