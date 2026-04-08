#include "persistence/PersistenceLayer.h"
#include "api/StateAPI.h"
#include "engine/Log.h"

PersistenceLayer::PersistenceLayer() {}

PersistenceLayer::~PersistenceLayer() {
    close();
}

void PersistenceLayer::open(const std::string& dbPath) {
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        perfLog("[Persistence] Failed to open database: %s\n", sqlite3_errmsg(db));
        return;
    }
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    createSchema();
    perfLog("[Persistence] Opened database: %s\n", dbPath.c_str());
}

void PersistenceLayer::close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

void PersistenceLayer::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        perfLog("[Persistence] SQL error: %s\n", msg.c_str());
    }
}

sqlite3_stmt* PersistenceLayer::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        perfLog("[Persistence] Prepare error: %s\n", sqlite3_errmsg(db));
    return stmt;
}

void PersistenceLayer::createSchema() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS plugins (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            manufacturer TEXT,
            format_id TEXT UNIQUE NOT NULL,
            is_instrument INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS presets (
            id TEXT PRIMARY KEY,
            plugin_id TEXT NOT NULL REFERENCES plugins(id),
            name TEXT NOT NULL,
            state_path TEXT NOT NULL,
            kind INTEGER DEFAULT 0,
            UNIQUE(plugin_id, name)
        );

        CREATE TABLE IF NOT EXISTS actions (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL UNIQUE,
            label TEXT,
            param_schema TEXT
        );

        CREATE TABLE IF NOT EXISTS songs (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            master_gain REAL DEFAULT 1.0,
            initial_state TEXT
        );

        CREATE TABLE IF NOT EXISTS tracks (
            id TEXT PRIMARY KEY,
            song_id TEXT NOT NULL REFERENCES songs(id) ON DELETE CASCADE,
            name TEXT NOT NULL,
            plugin_id TEXT REFERENCES plugins(id),
            preset_id TEXT REFERENCES presets(id),
            output_gain REAL DEFAULT 1.0,
            midi_enabled INTEGER DEFAULT 1,
            position INTEGER DEFAULT 0,
            processor_state TEXT,
            processor_state_hash TEXT
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
            parent_type TEXT NOT NULL CHECK(parent_type IN ('track', 'bus', 'song')),
            name TEXT NOT NULL,
            plugin_id TEXT NOT NULL REFERENCES plugins(id),
            preset_id TEXT REFERENCES presets(id),
            position INTEGER DEFAULT 0,
            processor_state TEXT,
            processor_state_hash TEXT
        );

        CREATE TABLE IF NOT EXISTS sends (
            id TEXT PRIMARY KEY,
            track_id TEXT NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
            bus_id TEXT NOT NULL REFERENCES busses(id),
            gain REAL DEFAULT 1.0
        );

        CREATE TABLE IF NOT EXISTS bindings (
            id TEXT PRIMARY KEY,
            song_id TEXT REFERENCES songs(id) ON DELETE CASCADE,
            control_type TEXT NOT NULL,
            channel INTEGER NOT NULL,
            number INTEGER NOT NULL,
            action_id TEXT NOT NULL REFERENCES actions(id),
            args TEXT,
            description TEXT,
            is_score_step INTEGER DEFAULT 0,
            score_position INTEGER DEFAULT -1
        );

        CREATE TABLE IF NOT EXISTS config (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )");

    // Migrations for existing databases
    sqlite3_exec(db, "ALTER TABLE tracks ADD COLUMN processor_state TEXT", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE tracks ADD COLUMN processor_state_hash TEXT", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE effects ADD COLUMN processor_state TEXT", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE effects ADD COLUMN processor_state_hash TEXT", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE bindings ADD COLUMN is_score_step INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE bindings ADD COLUMN score_position INTEGER DEFAULT -1", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DROP TABLE IF EXISTS score_steps", nullptr, nullptr, nullptr);
}

// ============================================================================
// Load
// ============================================================================

static std::string col_str(sqlite3_stmt* stmt, int i) {
    auto* text = sqlite3_column_text(stmt, i);
    return text ? std::string((const char*)text) : std::string();
}

void PersistenceLayer::loadInto(StateAPI& state) {
    // Build the full state from SQL — no StateAPI mutators, no events, no ID conflicts
    AppState loaded;
    readPlugins(loaded);
    readPresets(loaded);
    readActions(loaded);
    readSongs(loaded);
    readConfig(loaded);

    // Resolve currentSongId from config
    auto it = loaded.config.find("current_song_id");
    if (it != loaded.config.end()) {
        loaded.currentSongId = it->second;
        loaded.config.erase(it);  // not needed in the config map
    } else if (!loaded.songs.empty()) {
        loaded.currentSongId = loaded.songs[0].id;
    }

    // Atomic swap — fires one event, EngineSync rebuilds
    state.replaceState(std::move(loaded));
    perfLog("[Persistence] State loaded from database\n");
}

void PersistenceLayer::readPlugins(AppState& out) {
    auto* stmt = prepare("SELECT id, name, manufacturer, format_id, is_instrument FROM plugins");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.plugins.push_back({
            col_str(stmt, 0), col_str(stmt, 1), col_str(stmt, 2),
            col_str(stmt, 3), sqlite3_column_int(stmt, 4) != 0
        });
    }
    sqlite3_finalize(stmt);
}

void PersistenceLayer::readPresets(AppState& out) {
    auto* stmt = prepare("SELECT id, plugin_id, name, state_path, kind FROM presets");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.presets.push_back({
            col_str(stmt, 0), col_str(stmt, 1), col_str(stmt, 2),
            col_str(stmt, 3), static_cast<PresetKind>(sqlite3_column_int(stmt, 4))
        });
    }
    sqlite3_finalize(stmt);
}

void PersistenceLayer::readActions(AppState& out) {
    auto* stmt = prepare("SELECT id, name, label, param_schema FROM actions");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.actions.push_back({
            col_str(stmt, 0), col_str(stmt, 1), col_str(stmt, 2), col_str(stmt, 3)
        });
    }
    sqlite3_finalize(stmt);
}

void PersistenceLayer::readSongs(AppState& out) {
    auto* songStmt = prepare("SELECT id, name, master_gain, initial_state FROM songs");
    while (sqlite3_step(songStmt) == SQLITE_ROW) {
        SongState song;
        song.id = col_str(songStmt, 0);
        song.name = col_str(songStmt, 1);
        song.masterGain = (float)sqlite3_column_double(songStmt, 2);
        song.initialState = col_str(songStmt, 3);

        // Tracks
        auto* ts = prepare("SELECT id, name, plugin_id, preset_id, output_gain, midi_enabled, position, processor_state, processor_state_hash FROM tracks WHERE song_id = ? ORDER BY position");
        sqlite3_bind_text(ts, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(ts) == SQLITE_ROW) {
            TrackState t;
            t.id = col_str(ts, 0);
            t.name = col_str(ts, 1);
            t.pluginId = col_str(ts, 2);
            t.presetId = col_str(ts, 3);
            t.outputGain = (float)sqlite3_column_double(ts, 4);
            t.midiEnabled = sqlite3_column_int(ts, 5) != 0;
            t.position = sqlite3_column_int(ts, 6);
            t.processorState = col_str(ts, 7);
            t.processorStateHash = col_str(ts, 8);

            // Effects for this track
            auto* fxs = prepare("SELECT id, name, plugin_id, preset_id, position, processor_state, processor_state_hash FROM effects WHERE parent_id = ? AND parent_type = 'track' ORDER BY position");
            sqlite3_bind_text(fxs, 1, t.id.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(fxs) == SQLITE_ROW) {
                t.effects.push_back({
                    col_str(fxs, 0), col_str(fxs, 1), col_str(fxs, 2),
                    col_str(fxs, 3), sqlite3_column_int(fxs, 4),
                    LoadStatus::None, col_str(fxs, 5), col_str(fxs, 6)
                });
            }
            sqlite3_finalize(fxs);

            // Sends for this track
            auto* ss = prepare("SELECT id, bus_id, gain FROM sends WHERE track_id = ?");
            sqlite3_bind_text(ss, 1, t.id.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(ss) == SQLITE_ROW) {
                t.sends.push_back({ col_str(ss, 0), col_str(ss, 1), (float)sqlite3_column_double(ss, 2) });
            }
            sqlite3_finalize(ss);

            song.tracks.push_back(std::move(t));
        }
        sqlite3_finalize(ts);

        // Busses
        auto* bs = prepare("SELECT id, name, output_gain, position FROM busses WHERE song_id = ? ORDER BY position");
        sqlite3_bind_text(bs, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(bs) == SQLITE_ROW) {
            BusState b;
            b.id = col_str(bs, 0);
            b.name = col_str(bs, 1);
            b.outputGain = (float)sqlite3_column_double(bs, 2);
            b.position = sqlite3_column_int(bs, 3);

            // Effects for this bus
            auto* fxs = prepare("SELECT id, name, plugin_id, preset_id, position, processor_state, processor_state_hash FROM effects WHERE parent_id = ? AND parent_type = 'bus' ORDER BY position");
            sqlite3_bind_text(fxs, 1, b.id.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(fxs) == SQLITE_ROW) {
                b.effects.push_back({
                    col_str(fxs, 0), col_str(fxs, 1), col_str(fxs, 2),
                    col_str(fxs, 3), sqlite3_column_int(fxs, 4),
                    LoadStatus::None, col_str(fxs, 5), col_str(fxs, 6)
                });
            }
            sqlite3_finalize(fxs);

            song.busses.push_back(std::move(b));
        }
        sqlite3_finalize(bs);

        // Master effects
        auto* mfx = prepare("SELECT id, name, plugin_id, preset_id, position, processor_state, processor_state_hash FROM effects WHERE parent_id = ? AND parent_type = 'song' ORDER BY position");
        sqlite3_bind_text(mfx, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(mfx) == SQLITE_ROW) {
            song.masterEffects.push_back({
                col_str(mfx, 0), col_str(mfx, 1), col_str(mfx, 2),
                col_str(mfx, 3), sqlite3_column_int(mfx, 4),
                LoadStatus::None, col_str(mfx, 5), col_str(mfx, 6)
            });
        }
        sqlite3_finalize(mfx);

        // Song-scoped bindings (includes score steps via isScoreStep flag)
        auto* bi = prepare("SELECT id, control_type, channel, number, action_id, args, description, is_score_step, score_position FROM bindings WHERE song_id = ?");
        sqlite3_bind_text(bi, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(bi) == SQLITE_ROW) {
            song.bindings.push_back({
                col_str(bi, 0), song.id, col_str(bi, 1),
                sqlite3_column_int(bi, 2), sqlite3_column_int(bi, 3),
                col_str(bi, 4), col_str(bi, 5), col_str(bi, 6),
                sqlite3_column_int(bi, 7) != 0, sqlite3_column_int(bi, 8)
            });
        }
        sqlite3_finalize(bi);

        out.songs.push_back(std::move(song));
    }
    sqlite3_finalize(songStmt);

    // Global bindings
    auto* gb = prepare("SELECT id, control_type, channel, number, action_id, args, description, is_score_step, score_position FROM bindings WHERE song_id IS NULL");
    while (sqlite3_step(gb) == SQLITE_ROW) {
        out.globalBindings.push_back({
            col_str(gb, 0), "", col_str(gb, 1),
            sqlite3_column_int(gb, 2), sqlite3_column_int(gb, 3),
            col_str(gb, 4), col_str(gb, 5), col_str(gb, 6),
            sqlite3_column_int(gb, 7) != 0, sqlite3_column_int(gb, 8)
        });
    }
    sqlite3_finalize(gb);
}

void PersistenceLayer::readConfig(AppState& out) {
    auto* stmt = prepare("SELECT key, value FROM config");
    while (sqlite3_step(stmt) == SQLITE_ROW)
        out.config[col_str(stmt, 0)] = col_str(stmt, 1);
    sqlite3_finalize(stmt);
}

// ============================================================================
// Save
// ============================================================================

void PersistenceLayer::saveFrom(const StateAPI& state) {
    exec("BEGIN TRANSACTION");

    savePlugins(state);
    savePresets(state);
    saveActions(state);
    saveSongs(state);
    saveConfig(state);

    exec("COMMIT");
    perfLog("[Persistence] State saved to database\n");
}

void PersistenceLayer::savePlugins(const StateAPI& state) {
    for (auto& p : state.allPlugins()) {
        auto* stmt = prepare("INSERT OR REPLACE INTO plugins (id, name, manufacturer, format_id, is_instrument) VALUES (?, ?, ?, ?, ?)");
        sqlite3_bind_text(stmt, 1, p.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, p.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, p.manufacturer.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, p.formatId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, p.isInstrument ? 1 : 0);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void PersistenceLayer::savePresets(const StateAPI& state) {
    for (auto& p : state.appState().presets) {
        auto* stmt = prepare("INSERT OR REPLACE INTO presets (id, plugin_id, name, state_path, kind) VALUES (?, ?, ?, ?, ?)");
        sqlite3_bind_text(stmt, 1, p.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, p.pluginId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, p.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, p.statePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, static_cast<int>(p.kind));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void PersistenceLayer::saveActions(const StateAPI& state) {
    for (auto& a : state.allActions()) {
        auto* stmt = prepare("INSERT OR REPLACE INTO actions (id, name, label, param_schema) VALUES (?, ?, ?, ?)");
        sqlite3_bind_text(stmt, 1, a.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, a.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, a.label.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, a.paramSchema.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void PersistenceLayer::saveSongs(const StateAPI& state) {
    // Delete all existing data — effects have no FK cascade (polymorphic parent_id)
    exec("DELETE FROM effects");
    exec("DELETE FROM sends");
    exec("DELETE FROM songs");  // CASCADE clears tracks, busses, bindings
    // Delete global bindings separately (not cascaded)
    exec("DELETE FROM bindings WHERE song_id IS NULL");

    for (auto& song : state.allSongs()) {
        // Song
        auto* stmt = prepare("INSERT INTO songs (id, name, master_gain, initial_state) VALUES (?, ?, ?, ?)");
        sqlite3_bind_text(stmt, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, song.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, song.masterGain);
        if (!song.initialState.empty())
            sqlite3_bind_text(stmt, 4, song.initialState.c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 4);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        // Busses (before tracks, since sends reference busses)
        for (auto& b : song.busses) {
            auto* bs = prepare("INSERT INTO busses (id, song_id, name, output_gain, position) VALUES (?, ?, ?, ?, ?)");
            sqlite3_bind_text(bs, 1, b.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(bs, 2, song.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(bs, 3, b.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(bs, 4, b.outputGain);
            sqlite3_bind_int(bs, 5, b.position);
            sqlite3_step(bs);
            sqlite3_finalize(bs);

            // Bus effects
            for (auto& fx : b.effects) {
                auto* fs = prepare("INSERT INTO effects (id, parent_id, parent_type, name, plugin_id, preset_id, position, processor_state, processor_state_hash) VALUES (?, ?, 'bus', ?, ?, ?, ?, ?, ?)");
                sqlite3_bind_text(fs, 1, fx.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 2, b.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 3, fx.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 4, fx.pluginId.c_str(), -1, SQLITE_TRANSIENT);
                if (!fx.presetId.empty())
                    sqlite3_bind_text(fs, 5, fx.presetId.c_str(), -1, SQLITE_TRANSIENT);
                else sqlite3_bind_null(fs, 5);
                sqlite3_bind_int(fs, 6, fx.position);
                if (!fx.processorState.empty())
                    sqlite3_bind_text(fs, 7, fx.processorState.c_str(), -1, SQLITE_TRANSIENT);
                else sqlite3_bind_null(fs, 7);
                if (!fx.processorStateHash.empty())
                    sqlite3_bind_text(fs, 8, fx.processorStateHash.c_str(), -1, SQLITE_TRANSIENT);
                else sqlite3_bind_null(fs, 8);
                sqlite3_step(fs);
                sqlite3_finalize(fs);
            }
        }

        // Tracks (after busses, since sends reference bus IDs)
        perfLog("[Persistence] Saving %d tracks for song %s\n", (int)song.tracks.size(), song.name.c_str());
        for (auto& t : song.tracks) {
            perfLog("[Persistence]   Track %s: %d effects\n", t.name.c_str(), (int)t.effects.size());
            auto* ts = prepare("INSERT INTO tracks (id, song_id, name, plugin_id, preset_id, output_gain, midi_enabled, position, processor_state, processor_state_hash) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            sqlite3_bind_text(ts, 1, t.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ts, 2, song.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ts, 3, t.name.c_str(), -1, SQLITE_TRANSIENT);
            if (!t.pluginId.empty())
                sqlite3_bind_text(ts, 4, t.pluginId.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(ts, 4);
            if (!t.presetId.empty())
                sqlite3_bind_text(ts, 5, t.presetId.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(ts, 5);
            sqlite3_bind_double(ts, 6, t.outputGain);
            sqlite3_bind_int(ts, 7, t.midiEnabled ? 1 : 0);
            sqlite3_bind_int(ts, 8, t.position);
            if (!t.processorState.empty())
                sqlite3_bind_text(ts, 9, t.processorState.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(ts, 9);
            if (!t.processorStateHash.empty())
                sqlite3_bind_text(ts, 10, t.processorStateHash.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(ts, 10);
            sqlite3_step(ts);
            sqlite3_finalize(ts);

            // Track effects
            for (auto& fx : t.effects) {
                auto* fs = prepare("INSERT INTO effects (id, parent_id, parent_type, name, plugin_id, preset_id, position, processor_state, processor_state_hash) VALUES (?, ?, 'track', ?, ?, ?, ?, ?, ?)");
                sqlite3_bind_text(fs, 1, fx.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 2, t.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 3, fx.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 4, fx.pluginId.c_str(), -1, SQLITE_TRANSIENT);
                if (!fx.presetId.empty())
                    sqlite3_bind_text(fs, 5, fx.presetId.c_str(), -1, SQLITE_TRANSIENT);
                else sqlite3_bind_null(fs, 5);
                sqlite3_bind_int(fs, 6, fx.position);
                if (!fx.processorState.empty())
                    sqlite3_bind_text(fs, 7, fx.processorState.c_str(), -1, SQLITE_TRANSIENT);
                else sqlite3_bind_null(fs, 7);
                if (!fx.processorStateHash.empty())
                    sqlite3_bind_text(fs, 8, fx.processorStateHash.c_str(), -1, SQLITE_TRANSIENT);
                else sqlite3_bind_null(fs, 8);
                sqlite3_step(fs);
                sqlite3_finalize(fs);
            }

            // Sends
            for (auto& s : t.sends) {
                auto* ss = prepare("INSERT INTO sends (id, track_id, bus_id, gain) VALUES (?, ?, ?, ?)");
                sqlite3_bind_text(ss, 1, s.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ss, 2, t.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ss, 3, s.busId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(ss, 4, s.gain);
                sqlite3_step(ss);
                sqlite3_finalize(ss);
            }
        }

        // Master effects
        for (auto& fx : song.masterEffects) {
            auto* fs = prepare("INSERT INTO effects (id, parent_id, parent_type, name, plugin_id, preset_id, position, processor_state, processor_state_hash) VALUES (?, ?, 'song', ?, ?, ?, ?, ?, ?)");
            sqlite3_bind_text(fs, 1, fx.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(fs, 2, song.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(fs, 3, fx.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(fs, 4, fx.pluginId.c_str(), -1, SQLITE_TRANSIENT);
            if (!fx.presetId.empty())
                sqlite3_bind_text(fs, 5, fx.presetId.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(fs, 5);
            sqlite3_bind_int(fs, 6, fx.position);
            if (!fx.processorState.empty())
                sqlite3_bind_text(fs, 7, fx.processorState.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(fs, 7);
            if (!fx.processorStateHash.empty())
                sqlite3_bind_text(fs, 8, fx.processorStateHash.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(fs, 8);
            sqlite3_step(fs);
            sqlite3_finalize(fs);
        }

        // Song-scoped bindings (score steps stored as bindings with isScoreStep flag)
        for (auto& bind : song.bindings) {
            auto* bs = prepare("INSERT INTO bindings (id, song_id, control_type, channel, number, action_id, args, description, is_score_step, score_position) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            sqlite3_bind_text(bs, 1, bind.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(bs, 2, song.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(bs, 3, bind.controlType.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(bs, 4, bind.channel);
            sqlite3_bind_int(bs, 5, bind.number);
            sqlite3_bind_text(bs, 6, bind.actionId.c_str(), -1, SQLITE_TRANSIENT);
            if (!bind.args.empty())
                sqlite3_bind_text(bs, 7, bind.args.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(bs, 7);
            if (!bind.description.empty())
                sqlite3_bind_text(bs, 8, bind.description.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(bs, 8);
            sqlite3_bind_int(bs, 9, bind.isScoreStep ? 1 : 0);
            sqlite3_bind_int(bs, 10, bind.scorePosition);
            sqlite3_step(bs);
            sqlite3_finalize(bs);
        }
    }

    // Global bindings
    for (auto& bind : state.appState().globalBindings) {
        auto* bs = prepare("INSERT INTO bindings (id, song_id, control_type, channel, number, action_id, args, description, is_score_step, score_position) VALUES (?, NULL, ?, ?, ?, ?, ?, ?, ?, ?)");
        sqlite3_bind_text(bs, 1, bind.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(bs, 2, bind.controlType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(bs, 3, bind.channel);
        sqlite3_bind_int(bs, 4, bind.number);
        sqlite3_bind_text(bs, 5, bind.actionId.c_str(), -1, SQLITE_TRANSIENT);
        if (!bind.args.empty())
            sqlite3_bind_text(bs, 6, bind.args.c_str(), -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(bs, 6);
        if (!bind.description.empty())
            sqlite3_bind_text(bs, 7, bind.description.c_str(), -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(bs, 7);
        sqlite3_bind_int(bs, 8, bind.isScoreStep ? 1 : 0);
        sqlite3_bind_int(bs, 9, bind.scorePosition);
        sqlite3_step(bs);
        sqlite3_finalize(bs);
    }
}

void PersistenceLayer::saveConfig(const StateAPI& state) {
    exec("DELETE FROM config");

    // Save current song ID as config (not in the config map, but needs persisting)
    if (!state.appState().currentSongId.empty()) {
        auto* stmt = prepare("INSERT INTO config (key, value) VALUES ('current_song_id', ?)");
        sqlite3_bind_text(stmt, 1, state.appState().currentSongId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    for (auto& [key, value] : state.appState().config) {
        auto* stmt = prepare("INSERT INTO config (key, value) VALUES (?, ?)");
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}
