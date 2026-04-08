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

        CREATE TABLE IF NOT EXISTS score_steps (
            id TEXT PRIMARY KEY,
            song_id TEXT NOT NULL REFERENCES songs(id) ON DELETE CASCADE,
            position INTEGER NOT NULL,
            action_id TEXT REFERENCES actions(id),
            args TEXT,
            description TEXT
        );

        CREATE TABLE IF NOT EXISTS tracks (
            id TEXT PRIMARY KEY,
            song_id TEXT NOT NULL REFERENCES songs(id) ON DELETE CASCADE,
            name TEXT NOT NULL,
            plugin_id TEXT REFERENCES plugins(id),
            preset_id TEXT REFERENCES presets(id),
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
            parent_type TEXT NOT NULL CHECK(parent_type IN ('track', 'bus', 'song')),
            name TEXT NOT NULL,
            plugin_id TEXT NOT NULL REFERENCES plugins(id),
            preset_id TEXT REFERENCES presets(id),
            position INTEGER DEFAULT 0
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
            description TEXT
        );

        CREATE TABLE IF NOT EXISTS config (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )");
}

// ============================================================================
// Load
// ============================================================================

static std::string col_str(sqlite3_stmt* stmt, int i) {
    auto* text = sqlite3_column_text(stmt, i);
    return text ? std::string((const char*)text) : std::string();
}

void PersistenceLayer::loadInto(StateAPI& state) {
    loadPlugins(state);
    loadPresets(state);
    loadActions(state);
    loadSongs(state);
    loadConfig(state);

    // Now that all data is loaded, set the current song (triggers EngineSync)
    // Clear first so setCurrentSong always fires the event
    state.mutableState().currentSongId.clear();
    auto currentSongId = state.getConfig("current_song_id");
    if (!currentSongId.empty() && state.findSong(currentSongId))
        state.setCurrentSong(currentSongId);
    else if (!state.allSongs().empty())
        state.setCurrentSong(state.allSongs()[0].id);

    state.clearDirty();
    perfLog("[Persistence] State loaded from database\n");
}

void PersistenceLayer::loadPlugins(StateAPI& state) {
    auto* stmt = prepare("SELECT id, name, manufacturer, format_id, is_instrument FROM plugins");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto dbId = col_str(stmt, 0);
        auto name = col_str(stmt, 1);
        auto mfg = col_str(stmt, 2);
        auto fmtId = col_str(stmt, 3);
        bool isInst = sqlite3_column_int(stmt, 4) != 0;
        auto genId = state.registerPlugin(name, mfg, fmtId, isInst);
        // Preserve DB ID so FK references in tracks/effects match
        for (auto& p : state.mutableState().plugins) {
            if (p.id == genId) { p.id = dbId; break; }
        }
    }
    sqlite3_finalize(stmt);
}

void PersistenceLayer::loadPresets(StateAPI& state) {
    auto* stmt = prepare("SELECT id, plugin_id, name, state_path, kind FROM presets");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto dbId = col_str(stmt, 0);
        auto pluginId = col_str(stmt, 1);
        auto name = col_str(stmt, 2);
        auto path = col_str(stmt, 3);
        int kindInt = sqlite3_column_int(stmt, 4);
        auto kind = static_cast<PresetKind>(kindInt);
        auto genId = state.createPreset(pluginId, name, path, kind);
        for (auto& p : state.mutableState().presets) {
            if (p.id == genId) { p.id = dbId; break; }
        }
    }
    sqlite3_finalize(stmt);
}

void PersistenceLayer::loadActions(StateAPI& state) {
    auto* stmt = prepare("SELECT id, name, label, param_schema FROM actions");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto dbId = col_str(stmt, 0);
        auto name = col_str(stmt, 1);
        auto label = col_str(stmt, 2);
        auto schema = col_str(stmt, 3);
        auto genId = state.registerAction(name, label, schema);
        for (auto& a : state.mutableState().actions) {
            if (a.id == genId) { a.id = dbId; break; }
        }
    }
    sqlite3_finalize(stmt);
}

void PersistenceLayer::loadSongs(StateAPI& state) {
    // Load songs
    auto* songStmt = prepare("SELECT id, name, master_gain, initial_state FROM songs");
    while (sqlite3_step(songStmt) == SQLITE_ROW) {
        auto songDbId = col_str(songStmt, 0);
        auto name = col_str(songStmt, 1);
        float masterGain = (float)sqlite3_column_double(songStmt, 2);
        auto initialState = col_str(songStmt, 3);

        auto songId = state.createSong(name);

        // Overwrite the generated ID with the DB ID to preserve FK references
        auto* song = state.findSong(songId);
        if (!song) continue;
        song->id = songDbId;
        song->masterGain = masterGain;
        song->initialState = initialState;

        // Set current song directly (no event) so createTrack etc. work
        state.mutableState().currentSongId = songDbId;

        // Load tracks for this song
        auto* trackStmt = prepare("SELECT id, name, plugin_id, preset_id, output_gain, midi_enabled, position FROM tracks WHERE song_id = ? ORDER BY position");
        sqlite3_bind_text(trackStmt, 1, songDbId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(trackStmt) == SQLITE_ROW) {
            auto trackName = col_str(trackStmt, 1);
            auto trackId = state.createTrack(trackName);
            auto* track = state.findTrack(trackId);
            if (!track) continue;
            track->id = col_str(trackStmt, 0);  // preserve DB ID
            track->pluginId = col_str(trackStmt, 2);
            track->presetId = col_str(trackStmt, 3);
            track->outputGain = (float)sqlite3_column_double(trackStmt, 4);
            track->midiEnabled = sqlite3_column_int(trackStmt, 5) != 0;
            track->position = sqlite3_column_int(trackStmt, 6);
        }
        sqlite3_finalize(trackStmt);

        // Load busses for this song
        auto* busStmt = prepare("SELECT id, name, output_gain, position FROM busses WHERE song_id = ? ORDER BY position");
        sqlite3_bind_text(busStmt, 1, songDbId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(busStmt) == SQLITE_ROW) {
            auto busName = col_str(busStmt, 1);
            auto busId = state.createBus(busName);
            auto* bus = state.findBus(busId);
            if (!bus) continue;
            bus->id = col_str(busStmt, 0);
            bus->outputGain = (float)sqlite3_column_double(busStmt, 2);
            bus->position = sqlite3_column_int(busStmt, 3);
        }
        sqlite3_finalize(busStmt);

        // Load effects (for tracks, busses, and master)
        auto* fxStmt = prepare("SELECT id, parent_id, parent_type, name, plugin_id, preset_id, position FROM effects WHERE parent_id IN (SELECT id FROM tracks WHERE song_id = ?) OR parent_id IN (SELECT id FROM busses WHERE song_id = ?) OR (parent_id = ? AND parent_type = 'song') ORDER BY position");
        sqlite3_bind_text(fxStmt, 1, songDbId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(fxStmt, 2, songDbId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(fxStmt, 3, songDbId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(fxStmt) == SQLITE_ROW) {
            auto fxDbId = col_str(fxStmt, 0);
            auto parentId = col_str(fxStmt, 1);
            auto fxName = col_str(fxStmt, 3);
            auto pluginId = col_str(fxStmt, 4);

            auto fxId = state.addEffect(parentId, fxName, pluginId);
            // Find the effect and overwrite its ID with the DB ID
            // The effect was just added, so search for it by the generated ID
            auto* song2 = state.currentSong();
            if (!song2) continue;

            // Search all effect lists for the generated ID and replace
            auto replaceId = [&](std::vector<EffectState>& list) {
                for (auto& fx : list) {
                    if (fx.id == fxId) {
                        fx.id = fxDbId;
                        fx.presetId = col_str(fxStmt, 5);
                        fx.position = sqlite3_column_int(fxStmt, 6);
                        return true;
                    }
                }
                return false;
            };

            if (replaceId(song2->masterEffects)) continue;
            for (auto& t : song2->tracks)
                if (replaceId(t.effects)) break;
            for (auto& b : song2->busses)
                if (replaceId(b.effects)) break;
        }
        sqlite3_finalize(fxStmt);

        // Load sends
        auto* sendStmt = prepare("SELECT id, track_id, bus_id, gain FROM sends WHERE track_id IN (SELECT id FROM tracks WHERE song_id = ?)");
        sqlite3_bind_text(sendStmt, 1, songDbId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(sendStmt) == SQLITE_ROW) {
            auto sendDbId = col_str(sendStmt, 0);
            auto trackId = col_str(sendStmt, 1);
            auto busId = col_str(sendStmt, 2);
            float gain = (float)sqlite3_column_double(sendStmt, 3);

            auto sendId = state.addSend(trackId, busId, gain);
            // Replace generated ID with DB ID
            auto* track = state.findTrack(trackId);
            if (track) {
                for (auto& s : track->sends) {
                    if (s.id == sendId) { s.id = sendDbId; break; }
                }
            }
        }
        sqlite3_finalize(sendStmt);

        // Load score steps
        auto* scoreStmt = prepare("SELECT action_id, args, description FROM score_steps WHERE song_id = ? ORDER BY position");
        sqlite3_bind_text(scoreStmt, 1, songDbId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(scoreStmt) == SQLITE_ROW) {
            song->score.push_back({
                col_str(scoreStmt, 0),
                col_str(scoreStmt, 1),
                col_str(scoreStmt, 2)
            });
        }
        sqlite3_finalize(scoreStmt);

        // Load song-scoped bindings
        auto* bindStmt = prepare("SELECT id, control_type, channel, number, action_id, args, description FROM bindings WHERE song_id = ?");
        sqlite3_bind_text(bindStmt, 1, songDbId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(bindStmt) == SQLITE_ROW) {
            auto bindId = state.addBinding(songDbId,
                col_str(bindStmt, 1),
                sqlite3_column_int(bindStmt, 2),
                sqlite3_column_int(bindStmt, 3),
                col_str(bindStmt, 4),
                col_str(bindStmt, 5),
                col_str(bindStmt, 6));
            // Replace generated ID
            for (auto& b : song->bindings) {
                if (b.id == bindId) {
                    b.id = col_str(bindStmt, 0);
                    break;
                }
            }
        }
        sqlite3_finalize(bindStmt);

        // currentSongId already set to songDbId above
    }
    sqlite3_finalize(songStmt);

    // Load global bindings (song_id IS NULL)
    auto* globalBindStmt = prepare("SELECT id, control_type, channel, number, action_id, args, description FROM bindings WHERE song_id IS NULL");
    while (sqlite3_step(globalBindStmt) == SQLITE_ROW) {
        state.addGlobalBinding(
            col_str(globalBindStmt, 1),
            sqlite3_column_int(globalBindStmt, 2),
            sqlite3_column_int(globalBindStmt, 3),
            col_str(globalBindStmt, 4),
            col_str(globalBindStmt, 5),
            col_str(globalBindStmt, 6));
        // Overwrite the generated ID with the DB ID
        state.mutableState().globalBindings.back().id = col_str(globalBindStmt, 0);
    }
    sqlite3_finalize(globalBindStmt);
}

void PersistenceLayer::loadConfig(StateAPI& state) {
    auto* stmt = prepare("SELECT key, value FROM config");
    while (sqlite3_step(stmt) == SQLITE_ROW)
        state.setConfig(col_str(stmt, 0), col_str(stmt, 1));
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
    // Delete all existing songs (CASCADE clears tracks/busses/effects/sends/bindings/score_steps)
    exec("DELETE FROM songs");
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
                auto* fs = prepare("INSERT INTO effects (id, parent_id, parent_type, name, plugin_id, preset_id, position) VALUES (?, ?, 'bus', ?, ?, ?, ?)");
                sqlite3_bind_text(fs, 1, fx.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 2, b.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 3, fx.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 4, fx.pluginId.c_str(), -1, SQLITE_TRANSIENT);
                if (!fx.presetId.empty())
                    sqlite3_bind_text(fs, 5, fx.presetId.c_str(), -1, SQLITE_TRANSIENT);
                else sqlite3_bind_null(fs, 5);
                sqlite3_bind_int(fs, 6, fx.position);
                sqlite3_step(fs);
                sqlite3_finalize(fs);
            }
        }

        // Tracks (after busses, since sends reference bus IDs)
        for (auto& t : song.tracks) {
            auto* ts = prepare("INSERT INTO tracks (id, song_id, name, plugin_id, preset_id, output_gain, midi_enabled, position) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
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
            sqlite3_step(ts);
            sqlite3_finalize(ts);

            // Track effects
            for (auto& fx : t.effects) {
                auto* fs = prepare("INSERT INTO effects (id, parent_id, parent_type, name, plugin_id, preset_id, position) VALUES (?, ?, 'track', ?, ?, ?, ?)");
                sqlite3_bind_text(fs, 1, fx.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 2, t.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 3, fx.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fs, 4, fx.pluginId.c_str(), -1, SQLITE_TRANSIENT);
                if (!fx.presetId.empty())
                    sqlite3_bind_text(fs, 5, fx.presetId.c_str(), -1, SQLITE_TRANSIENT);
                else sqlite3_bind_null(fs, 5);
                sqlite3_bind_int(fs, 6, fx.position);
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
            auto* fs = prepare("INSERT INTO effects (id, parent_id, parent_type, name, plugin_id, preset_id, position) VALUES (?, ?, 'song', ?, ?, ?, ?)");
            sqlite3_bind_text(fs, 1, fx.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(fs, 2, song.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(fs, 3, fx.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(fs, 4, fx.pluginId.c_str(), -1, SQLITE_TRANSIENT);
            if (!fx.presetId.empty())
                sqlite3_bind_text(fs, 5, fx.presetId.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(fs, 5);
            sqlite3_bind_int(fs, 6, fx.position);
            sqlite3_step(fs);
            sqlite3_finalize(fs);
        }

        // Score steps
        for (int i = 0; i < (int)song.score.size(); ++i) {
            auto& step = song.score[i];
            auto stepId = StateAPI::generateId();
            auto* ss = prepare("INSERT INTO score_steps (id, song_id, position, action_id, args, description) VALUES (?, ?, ?, ?, ?, ?)");
            sqlite3_bind_text(ss, 1, stepId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ss, 2, song.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ss, 3, i);
            sqlite3_bind_text(ss, 4, step.actionId.c_str(), -1, SQLITE_TRANSIENT);
            if (!step.args.empty())
                sqlite3_bind_text(ss, 5, step.args.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(ss, 5);
            if (!step.description.empty())
                sqlite3_bind_text(ss, 6, step.description.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(ss, 6);
            sqlite3_step(ss);
            sqlite3_finalize(ss);
        }

        // Song-scoped bindings
        for (auto& bind : song.bindings) {
            auto* bs = prepare("INSERT INTO bindings (id, song_id, control_type, channel, number, action_id, args, description) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
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
            sqlite3_step(bs);
            sqlite3_finalize(bs);
        }
    }

    // Global bindings
    for (auto& bind : state.appState().globalBindings) {
        auto* bs = prepare("INSERT INTO bindings (id, song_id, control_type, channel, number, action_id, args, description) VALUES (?, NULL, ?, ?, ?, ?, ?, ?)");
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
