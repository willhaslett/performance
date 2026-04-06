#include "registry/Registry.h"
#include "engine/Log.h"
#include <stdexcept>

Registry::Registry() {}

Registry::~Registry() {
    close();
}

void Registry::open(const std::string& dbPath) {
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        perfLog("[Registry] Failed to open database: %s\n", sqlite3_errmsg(db));
        return;
    }
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    createSchema();
    perfLog("[Registry] Opened database: %s\n", dbPath.c_str());
}

void Registry::close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

void Registry::createSchema() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS plugins (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            manufacturer TEXT,
            format_id TEXT UNIQUE NOT NULL
        );

        CREATE TABLE IF NOT EXISTS snapshots (
            id TEXT PRIMARY KEY,
            plugin_id TEXT NOT NULL REFERENCES plugins(id),
            name TEXT NOT NULL,
            state_path TEXT NOT NULL,
            UNIQUE(plugin_id, name)
        );

        CREATE TABLE IF NOT EXISTS songs (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL UNIQUE,
            initial_state TEXT,
            score TEXT,
            master_gain REAL DEFAULT 1.0
        );

        CREATE TABLE IF NOT EXISTS tracks (
            id TEXT PRIMARY KEY,
            song_id TEXT NOT NULL REFERENCES songs(id) ON DELETE CASCADE,
            name TEXT NOT NULL,
            plugin_id TEXT REFERENCES plugins(id),
            snapshot_id TEXT REFERENCES snapshots(id),
            output_gain REAL DEFAULT 1.0,
            midi_enabled INTEGER DEFAULT 1,
            position INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS busses (
            id TEXT PRIMARY KEY,
            song_id TEXT NOT NULL REFERENCES songs(id) ON DELETE CASCADE,
            name TEXT NOT NULL,
            output_gain REAL DEFAULT 1.0,
            position INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS effects (
            id TEXT PRIMARY KEY,
            parent_id TEXT NOT NULL,
            parent_type TEXT NOT NULL CHECK(parent_type IN ('track', 'bus')),
            name TEXT NOT NULL,
            plugin_id TEXT NOT NULL REFERENCES plugins(id),
            snapshot_id TEXT REFERENCES snapshots(id),
            position INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS sends (
            id TEXT PRIMARY KEY,
            track_id TEXT NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
            bus_id TEXT NOT NULL REFERENCES busses(id),
            gain REAL DEFAULT 1.0
        );

        CREATE TABLE IF NOT EXISTS actions (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL UNIQUE,
            label TEXT,
            param_schema TEXT
        );

        CREATE TABLE IF NOT EXISTS bindings (
            id TEXT PRIMARY KEY,
            song_id TEXT NOT NULL REFERENCES songs(id) ON DELETE CASCADE,
            control_type TEXT NOT NULL CHECK(control_type IN ('cc', 'note', 'pitchbend', 'pressure')),
            channel INTEGER NOT NULL,
            number INTEGER NOT NULL,
            action_id TEXT NOT NULL REFERENCES actions(id),
            args TEXT,
            description TEXT
        );
    )");

    // Migrations for existing databases
    sqlite3_exec(db, "ALTER TABLE songs ADD COLUMN master_gain REAL DEFAULT 1.0", nullptr, nullptr, nullptr);
}

std::string Registry::generateId() {
    return juce::Uuid().toString().toStdString();
}

void Registry::exec(const std::string& sql) const {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        perfLog("[Registry] SQL error: %s\n", msg.c_str());
    }
}

sqlite3_stmt* Registry::prepare(const std::string& sql) const {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    return stmt;
}

// --- Plugins ---

std::string Registry::registerPlugin(const std::string& name, const std::string& manufacturer,
                                      const std::string& formatId) {
    // Check if already exists
    auto existing = findPluginByName(name);
    if (existing) return existing->id;

    auto id = generateId();
    auto* stmt = prepare("INSERT OR IGNORE INTO plugins (id, name, manufacturer, format_id) VALUES (?, ?, ?, ?)");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, manufacturer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, formatId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return id;
}

std::optional<Registry::Plugin> Registry::findPluginByName(const std::string& name) const {
    auto* stmt = prepare("SELECT id, name, manufacturer, format_id FROM plugins WHERE name = ?");
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Plugin> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Plugin{
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "",
            (const char*)sqlite3_column_text(stmt, 3)
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<Registry::Plugin> Registry::findPluginById(const std::string& id) const {
    auto* stmt = prepare("SELECT id, name, manufacturer, format_id FROM plugins WHERE id = ?");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Plugin> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Plugin{
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "",
            (const char*)sqlite3_column_text(stmt, 3)
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<Registry::Plugin> Registry::allPlugins() const {
    std::vector<Plugin> result;
    auto* stmt = prepare("SELECT id, name, manufacturer, format_id FROM plugins ORDER BY name");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "",
            (const char*)sqlite3_column_text(stmt, 3)
        });
    }
    sqlite3_finalize(stmt);
    return result;
}

// --- Snapshots ---

std::string Registry::createSnapshot(const std::string& pluginId, const std::string& name,
                                      const std::string& statePath) {
    auto existing = findSnapshot(pluginId, name);
    if (existing) return existing->id;

    auto id = generateId();
    auto* stmt = prepare("INSERT INTO snapshots (id, plugin_id, name, state_path) VALUES (?, ?, ?, ?)");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pluginId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, statePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return id;
}

std::optional<Registry::Snapshot> Registry::findSnapshot(const std::string& pluginId,
                                                          const std::string& name) const {
    auto* stmt = prepare("SELECT id, plugin_id, name, state_path FROM snapshots WHERE plugin_id = ? AND name = ?");
    sqlite3_bind_text(stmt, 1, pluginId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Snapshot> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Snapshot{
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (const char*)sqlite3_column_text(stmt, 2),
            (const char*)sqlite3_column_text(stmt, 3)
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<Registry::Snapshot> Registry::findSnapshotById(const std::string& id) const {
    auto* stmt = prepare("SELECT id, plugin_id, name, state_path FROM snapshots WHERE id = ?");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Snapshot> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Snapshot{
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (const char*)sqlite3_column_text(stmt, 2),
            (const char*)sqlite3_column_text(stmt, 3)
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<Registry::Snapshot> Registry::snapshotsForPlugin(const std::string& pluginId) const {
    std::vector<Snapshot> result;
    auto* stmt = prepare("SELECT id, plugin_id, name, state_path FROM snapshots WHERE plugin_id = ? ORDER BY name");
    sqlite3_bind_text(stmt, 1, pluginId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (const char*)sqlite3_column_text(stmt, 2),
            (const char*)sqlite3_column_text(stmt, 3)
        });
    }
    sqlite3_finalize(stmt);
    return result;
}

// --- Songs ---

std::string Registry::createSong(const std::string& name) {
    auto existing = findSongByName(name);
    if (existing) return existing->id;

    auto id = generateId();
    auto* stmt = prepare("INSERT INTO songs (id, name) VALUES (?, ?)");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    eventBus.emit({ RegistryEvent::Created, EntityType::Song, id });
    return id;
}

std::optional<Registry::Song> Registry::findSongByName(const std::string& name) const {
    auto* stmt = prepare("SELECT id, name FROM songs WHERE name = ?");
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Song> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Song{
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1)
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<Registry::Song> Registry::findSongById(const std::string& id) const {
    auto* stmt = prepare("SELECT id, name FROM songs WHERE id = ?");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Song> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Song{
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1)
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<Registry::Song> Registry::allSongs() const {
    std::vector<Song> result;
    auto* stmt = prepare("SELECT id, name FROM songs ORDER BY name");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1)
        });
    }
    sqlite3_finalize(stmt);
    return result;
}

void Registry::deleteSong(const std::string& id) {
    auto* stmt = prepare("DELETE FROM songs WHERE id = ?");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    eventBus.emit({ RegistryEvent::Deleted, EntityType::Song, id });
}

void Registry::setMasterGain(const std::string& songId, float gain) {
    auto* stmt = prepare("UPDATE songs SET master_gain = ? WHERE id = ?");
    sqlite3_bind_double(stmt, 1, gain);
    sqlite3_bind_text(stmt, 2, songId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

float Registry::getMasterGain(const std::string& songId) const {
    auto* stmt = prepare("SELECT master_gain FROM songs WHERE id = ?");
    sqlite3_bind_text(stmt, 1, songId.c_str(), -1, SQLITE_TRANSIENT);
    float gain = 1.0f;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        gain = (float)sqlite3_column_double(stmt, 0);
    sqlite3_finalize(stmt);
    return gain;
}

// --- Tracks ---

std::string Registry::createTrack(const std::string& songId, const std::string& name,
                                    const std::string& pluginId, const std::string& snapshotId,
                                    float outputGain, bool midiEnabled) {
    auto id = generateId();
    auto* stmt = prepare(
        "INSERT INTO tracks (id, song_id, name, plugin_id, snapshot_id, output_gain, midi_enabled, position) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, (SELECT COALESCE(MAX(position), -1) + 1 FROM tracks WHERE song_id = ?))");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, songId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, pluginId.empty() ? nullptr : pluginId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, snapshotId.empty() ? nullptr : snapshotId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, outputGain);
    sqlite3_bind_int(stmt, 7, midiEnabled ? 1 : 0);
    sqlite3_bind_text(stmt, 8, songId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    eventBus.emit({ RegistryEvent::Created, EntityType::Track, id });
    return id;
}

std::vector<Registry::Track> Registry::tracksForSong(const std::string& songId) const {
    std::vector<Track> result;
    auto* stmt = prepare(
        "SELECT id, song_id, name, plugin_id, snapshot_id, output_gain, midi_enabled, position "
        "FROM tracks WHERE song_id = ? ORDER BY position");
    sqlite3_bind_text(stmt, 1, songId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Track t;
        t.id = (const char*)sqlite3_column_text(stmt, 0);
        t.songId = (const char*)sqlite3_column_text(stmt, 1);
        t.name = (const char*)sqlite3_column_text(stmt, 2);
        t.pluginId = sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "";
        t.snapshotId = sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "";
        t.outputGain = (float)sqlite3_column_double(stmt, 5);
        t.midiEnabled = sqlite3_column_int(stmt, 6) != 0;
        t.position = sqlite3_column_int(stmt, 7);
        result.push_back(t);
    }
    sqlite3_finalize(stmt);
    return result;
}

void Registry::deleteTrack(const std::string& id) {
    auto* stmt = prepare("DELETE FROM tracks WHERE id = ?");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Registry::updateTrack(const Track& track) {
    auto* stmt = prepare(
        "UPDATE tracks SET name = ?, plugin_id = ?, snapshot_id = ?, "
        "output_gain = ?, midi_enabled = ?, position = ? WHERE id = ?");
    sqlite3_bind_text(stmt, 1, track.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track.pluginId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, track.snapshotId.empty() ? nullptr : track.snapshotId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, track.outputGain);
    sqlite3_bind_int(stmt, 5, track.midiEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 6, track.position);
    sqlite3_bind_text(stmt, 7, track.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// --- Busses ---

std::string Registry::createBus(const std::string& songId, const std::string& name,
                                  float outputGain) {
    auto id = generateId();
    auto* stmt = prepare(
        "INSERT INTO busses (id, song_id, name, output_gain, position) "
        "VALUES (?, ?, ?, ?, (SELECT COALESCE(MAX(position), -1) + 1 FROM busses WHERE song_id = ?))");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, songId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, outputGain);
    sqlite3_bind_text(stmt, 5, songId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    eventBus.emit({ RegistryEvent::Created, EntityType::Bus, id });
    return id;
}

std::vector<Registry::Bus> Registry::bussesForSong(const std::string& songId) const {
    std::vector<Bus> result;
    auto* stmt = prepare("SELECT id, song_id, name, output_gain, position FROM busses WHERE song_id = ? ORDER BY position");
    sqlite3_bind_text(stmt, 1, songId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (const char*)sqlite3_column_text(stmt, 2),
            (float)sqlite3_column_double(stmt, 3),
            sqlite3_column_int(stmt, 4)
        });
    }
    sqlite3_finalize(stmt);
    return result;
}

void Registry::deleteBus(const std::string& id) {
    auto* stmt = prepare("DELETE FROM busses WHERE id = ?");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// --- Effects ---

std::string Registry::createEffect(const std::string& parentId, const std::string& parentType,
                                     const std::string& name, const std::string& pluginId,
                                     const std::string& snapshotId) {
    auto id = generateId();
    auto* stmt = prepare(
        "INSERT INTO effects (id, parent_id, parent_type, name, plugin_id, snapshot_id, position) "
        "VALUES (?, ?, ?, ?, ?, ?, (SELECT COALESCE(MAX(position), -1) + 1 FROM effects WHERE parent_id = ?))");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, parentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, parentType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, pluginId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, snapshotId.empty() ? nullptr : snapshotId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, parentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    eventBus.emit({ RegistryEvent::Created, EntityType::Effect, id });
    return id;
}

std::vector<Registry::Effect> Registry::effectsForParent(const std::string& parentId) const {
    std::vector<Effect> result;
    auto* stmt = prepare(
        "SELECT id, parent_id, parent_type, name, plugin_id, snapshot_id, position "
        "FROM effects WHERE parent_id = ? ORDER BY position");
    sqlite3_bind_text(stmt, 1, parentId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (const char*)sqlite3_column_text(stmt, 2),
            (const char*)sqlite3_column_text(stmt, 3),
            (const char*)sqlite3_column_text(stmt, 4),
            sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "",
            sqlite3_column_int(stmt, 6)
        });
    }
    sqlite3_finalize(stmt);
    return result;
}

// --- Sends ---

std::string Registry::createSend(const std::string& trackId, const std::string& busId,
                                   float gain) {
    auto id = generateId();
    auto* stmt = prepare("INSERT INTO sends (id, track_id, bus_id, gain) VALUES (?, ?, ?, ?)");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, trackId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, busId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, gain);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    eventBus.emit({ RegistryEvent::Created, EntityType::Send, id });
    return id;
}

std::vector<Registry::Send> Registry::sendsForTrack(const std::string& trackId) const {
    std::vector<Send> result;
    auto* stmt = prepare("SELECT id, track_id, bus_id, gain FROM sends WHERE track_id = ?");
    sqlite3_bind_text(stmt, 1, trackId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (const char*)sqlite3_column_text(stmt, 2),
            (float)sqlite3_column_double(stmt, 3)
        });
    }
    sqlite3_finalize(stmt);
    return result;
}

// --- Actions ---

std::string Registry::registerAction(const std::string& name, const std::string& label,
                                      const std::string& paramSchema) {
    auto existing = findActionByName(name);
    if (existing) return existing->id;

    auto id = generateId();
    auto* stmt = prepare("INSERT INTO actions (id, name, label, param_schema) VALUES (?, ?, ?, ?)");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, label.empty() ? name.c_str() : label.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, paramSchema.empty() ? nullptr : paramSchema.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return id;
}

std::optional<Registry::Action> Registry::findActionByName(const std::string& name) const {
    auto* stmt = prepare("SELECT id, name, label, param_schema FROM actions WHERE name = ?");
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Action> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Action{
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "",
            sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : ""
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<Registry::Action> Registry::findActionById(const std::string& id) const {
    auto* stmt = prepare("SELECT id, name, label, param_schema FROM actions WHERE id = ?");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Action> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = Action{
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "",
            sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : ""
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<Registry::Action> Registry::allActions() const {
    std::vector<Action> result;
    auto* stmt = prepare("SELECT id, name, label, param_schema FROM actions ORDER BY name");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "",
            sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : ""
        });
    }
    sqlite3_finalize(stmt);
    return result;
}

// --- Bindings ---

std::string Registry::createBinding(const std::string& songId, const std::string& controlType,
                                      int channel, int number, const std::string& actionId,
                                      const std::string& args, const std::string& description) {
    auto id = generateId();
    auto* stmt = prepare(
        "INSERT INTO bindings (id, song_id, control_type, channel, number, action_id, args, description) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, songId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, controlType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, channel);
    sqlite3_bind_int(stmt, 5, number);
    sqlite3_bind_text(stmt, 6, actionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, args.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, description.empty() ? nullptr : description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return id;
}

std::vector<Registry::Binding> Registry::bindingsForSong(const std::string& songId) const {
    std::vector<Binding> result;
    auto* stmt = prepare(
        "SELECT id, song_id, control_type, channel, number, action_id, args, description "
        "FROM bindings WHERE song_id = ?");
    sqlite3_bind_text(stmt, 1, songId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (const char*)sqlite3_column_text(stmt, 2),
            sqlite3_column_int(stmt, 3),
            sqlite3_column_int(stmt, 4),
            (const char*)sqlite3_column_text(stmt, 5),
            sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "[]",
            sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : ""
        });
    }
    sqlite3_finalize(stmt);
    return result;
}

void Registry::deleteBinding(const std::string& id) {
    auto* stmt = prepare("DELETE FROM bindings WHERE id = ?");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// --- Generic CRUD ---

static const std::map<std::string, std::string> typeToTable = {
    {EntityType::Plugin, "plugins"}, {EntityType::Snapshot, "snapshots"},
    {EntityType::Song, "songs"}, {EntityType::Track, "tracks"},
    {EntityType::Bus, "busses"}, {EntityType::Effect, "effects"},
    {EntityType::Send, "sends"}, {EntityType::Action, "actions"},
    {EntityType::Binding, "bindings"}
};

static std::string tableForType(const std::string& type) {
    auto it = typeToTable.find(type);
    return it != typeToTable.end() ? it->second : "";
}

// Reverse: figure out type from which table has this ID
static std::string typeForId(sqlite3* db, const std::string& id) {
    for (auto& [type, table] : typeToTable) {
        auto sql = "SELECT 1 FROM " + table + " WHERE id = ?";
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        bool found = sqlite3_step(stmt) == SQLITE_ROW;
        sqlite3_finalize(stmt);
        if (found) return type;
    }
    return "";
}

std::string Registry::create(const std::string& type, const std::map<std::string, std::string>& fields) {
    auto table = tableForType(type);
    if (table.empty()) {
        perfLog("[Registry] Unknown type: %s\n", type.c_str());
        return "";
    }

    auto id = generateId();

    std::string cols = "id";
    std::string vals = "?";
    std::vector<std::string> values = { id };

    for (auto& [key, value] : fields) {
        cols += ", " + key;
        vals += ", ?";
        values.push_back(value);
    }

    auto sql = "INSERT INTO " + table + " (" + cols + ") VALUES (" + vals + ")";
    auto* stmt = prepare(sql);
    for (int i = 0; i < (int)values.size(); ++i)
        sqlite3_bind_text(stmt, i + 1, values[i].c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        perfLog("[Registry] Create failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return "";
    }
    sqlite3_finalize(stmt);
    eventBus.emit({ RegistryEvent::Created, type, id });
    return id;
}

std::optional<Entity> Registry::get(const std::string& id) {
    auto type = typeForId(db, id);
    if (type.empty()) return std::nullopt;

    auto table = tableForType(type);
    auto sql = "SELECT * FROM " + table + " WHERE id = ?";
    auto* stmt = prepare(sql);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    Entity entity;
    entity.id = id;
    entity.type = type;
    int colCount = sqlite3_column_count(stmt);
    for (int i = 0; i < colCount; ++i) {
        auto colName = std::string(sqlite3_column_name(stmt, i));
        auto text = sqlite3_column_text(stmt, i);
        entity.fields[colName] = text ? std::string((const char*)text) : "";
    }

    sqlite3_finalize(stmt);
    return entity;
}

std::vector<Entity> Registry::list(const std::string& type,
                                    const std::map<std::string, std::string>& filters) {
    std::vector<Entity> result;
    auto table = tableForType(type);
    if (table.empty()) return result;

    auto sql = "SELECT * FROM " + table;
    std::vector<std::string> filterValues;

    if (!filters.empty()) {
        sql += " WHERE ";
        bool first = true;
        for (auto& [key, value] : filters) {
            if (!first) sql += " AND ";
            sql += key + " = ?";
            filterValues.push_back(value);
            first = false;
        }
    }

    sql += " ORDER BY id";

    auto* stmt = prepare(sql);
    for (int i = 0; i < (int)filterValues.size(); ++i)
        sqlite3_bind_text(stmt, i + 1, filterValues[i].c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Entity entity;
        entity.type = type;
        int colCount = sqlite3_column_count(stmt);
        for (int i = 0; i < colCount; ++i) {
            auto colName = std::string(sqlite3_column_name(stmt, i));
            auto text = sqlite3_column_text(stmt, i);
            entity.fields[colName] = text ? std::string((const char*)text) : "";
            if (colName == "id") entity.id = entity.fields[colName];
        }
        result.push_back(entity);
    }

    sqlite3_finalize(stmt);
    return result;
}

void Registry::update(const std::string& id, const std::map<std::string, std::string>& fields) {
    auto type = typeForId(db, id);
    if (type.empty()) return;

    auto table = tableForType(type);
    std::string sql = "UPDATE " + table + " SET ";
    std::vector<std::string> values;

    bool first = true;
    for (auto& [key, value] : fields) {
        if (!first) sql += ", ";
        sql += key + " = ?";
        values.push_back(value);
        first = false;
    }
    sql += " WHERE id = ?";
    values.push_back(id);

    auto* stmt = prepare(sql);
    for (int i = 0; i < (int)values.size(); ++i)
        sqlite3_bind_text(stmt, i + 1, values[i].c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    eventBus.emit({ RegistryEvent::Updated, type, id });
}

void Registry::remove(const std::string& id) {
    auto type = typeForId(db, id);
    if (type.empty()) return;

    auto table = tableForType(type);
    auto* stmt = prepare("DELETE FROM " + table + " WHERE id = ?");
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    eventBus.emit({ RegistryEvent::Deleted, type, id });
}
