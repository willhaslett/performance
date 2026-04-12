#include "persistence/PersistenceLayer.h"
#include "api/StateAPI.h"
#include "engine/Log.h"
#include <juce_core/juce_core.h>

PersistenceLayer::PersistenceLayer() {}

PersistenceLayer::~PersistenceLayer() {
    close();
}

void PersistenceLayer::open(const std::string& dbPath) {
    this->dbPath = dbPath;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        perfLog("[Persistence] Failed to open database: %s\n", sqlite3_errmsg(db));
        return;
    }
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    createSchema();

    // Set schema version if not present
    auto* stmt = prepare("SELECT value FROM config WHERE key = 'schema_version'");
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        exec("INSERT OR REPLACE INTO config (key, value) VALUES ('schema_version', '1')");
    } else {
        sqlite3_finalize(stmt);
    }

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
            param_schema TEXT,
            lua_code TEXT,
            song_id TEXT
        );

        CREATE TABLE IF NOT EXISTS songs (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            master_gain REAL DEFAULT 1.0,
            initial_state TEXT,
            tempo REAL DEFAULT 120.0,
            time_sig_num INTEGER DEFAULT 4,
            time_sig_den INTEGER DEFAULT 4
        );

        CREATE TABLE IF NOT EXISTS tracks (
            id TEXT PRIMARY KEY,
            song_id TEXT NOT NULL REFERENCES songs(id) ON DELETE CASCADE,
            name TEXT NOT NULL,
            plugin_id TEXT REFERENCES plugins(id),
            preset_id TEXT REFERENCES presets(id),
            output_gain REAL DEFAULT 1.0,
            midi_enabled INTEGER DEFAULT 1,
            audio_enabled INTEGER DEFAULT 1,
            color INTEGER DEFAULT 0,
            output_target TEXT DEFAULT '',
            position INTEGER DEFAULT 0,
            processor_state TEXT,
            processor_state_hash TEXT,
            source_type TEXT DEFAULT 'instrument',
            channel_mode TEXT DEFAULT 'stereo',
            input_channel_start INTEGER DEFAULT -1,
            input_channel_count INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS busses (
            id TEXT PRIMARY KEY,
            song_id TEXT NOT NULL REFERENCES songs(id) ON DELETE CASCADE,
            name TEXT NOT NULL,
            output_gain REAL DEFAULT 1.0,
            position INTEGER DEFAULT 0,
            output_target TEXT DEFAULT ''
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
    sqlite3_exec(db, "ALTER TABLE bindings ADD COLUMN device_id TEXT", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE tracks ADD COLUMN audio_enabled INTEGER DEFAULT 1", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE tracks ADD COLUMN source_type TEXT DEFAULT 'instrument'", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE tracks ADD COLUMN channel_mode TEXT DEFAULT 'stereo'", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE tracks ADD COLUMN input_channel_start INTEGER DEFAULT -1", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE tracks ADD COLUMN input_channel_count INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DROP TABLE IF EXISTS score_steps", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DROP TABLE IF EXISTS region_events", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE regions ADD COLUMN active_take_id TEXT DEFAULT ''", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE tracks ADD COLUMN color INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE songs ADD COLUMN tempo REAL DEFAULT 120.0", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE songs ADD COLUMN time_sig_num INTEGER DEFAULT 4", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE songs ADD COLUMN time_sig_den INTEGER DEFAULT 4", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE tracks ADD COLUMN output_target TEXT DEFAULT ''", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE busses ADD COLUMN output_target TEXT DEFAULT ''", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE regions ADD COLUMN muted INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE regions ADD COLUMN quantize REAL DEFAULT 0.0", nullptr, nullptr, nullptr);

    // Action events table
    exec(R"(
        CREATE TABLE IF NOT EXISTS action_events (
            id TEXT PRIMARY KEY,
            track_id TEXT NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
            beat REAL NOT NULL,
            action_id TEXT NOT NULL,
            args_json TEXT DEFAULT '[]'
        );
    )");

    // Arrangement tables
    exec(R"(
        CREATE TABLE IF NOT EXISTS regions (
            id TEXT PRIMARY KEY,
            track_id TEXT NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
            type TEXT NOT NULL DEFAULT 'midi',
            name TEXT DEFAULT '',
            start_beat REAL NOT NULL,
            length_beats REAL NOT NULL,
            active_take_id TEXT DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS takes (
            id TEXT PRIMARY KEY,
            region_id TEXT NOT NULL REFERENCES regions(id) ON DELETE CASCADE,
            name TEXT DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS take_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            take_id TEXT NOT NULL REFERENCES takes(id) ON DELETE CASCADE,
            beat_offset REAL NOT NULL,
            status INTEGER NOT NULL,
            channel INTEGER NOT NULL,
            data1 INTEGER NOT NULL,
            data2 INTEGER NOT NULL
        );
    )");

    // Device tables
    exec(R"(
        CREATE TABLE IF NOT EXISTS devices (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            midi_port_name TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS device_controls (
            id TEXT PRIMARY KEY,
            device_id TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
            name TEXT NOT NULL,
            control_type TEXT NOT NULL,
            channel INTEGER NOT NULL,
            number INTEGER NOT NULL,
            group_name TEXT,
            position INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS song_devices (
            song_id TEXT NOT NULL REFERENCES songs(id) ON DELETE CASCADE,
            device_id TEXT NOT NULL REFERENCES devices(id),
            PRIMARY KEY (song_id, device_id)
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
    // Build the full state from SQL — no StateAPI mutators, no events, no ID conflicts
    AppState loaded;
    readPlugins(loaded);
    readPresets(loaded);
    readActions(loaded);
    readDevices(loaded);
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
    auto* stmt = prepare("SELECT id, name, label, param_schema, lua_code, song_id FROM actions");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ActionInfo a;
        a.id = col_str(stmt, 0);
        a.name = col_str(stmt, 1);
        a.label = col_str(stmt, 2);
        a.paramSchema = col_str(stmt, 3);
        a.luaCode = col_str(stmt, 4);
        a.songId = col_str(stmt, 5);
        out.actions.push_back(std::move(a));
    }
    sqlite3_finalize(stmt);
}

void PersistenceLayer::readSongs(AppState& out) {
    auto* songStmt = prepare("SELECT id, name, master_gain, initial_state, tempo, time_sig_num, time_sig_den FROM songs");
    while (sqlite3_step(songStmt) == SQLITE_ROW) {
        SongState song;
        song.id = col_str(songStmt, 0);
        song.name = col_str(songStmt, 1);
        song.masterGain = (float)sqlite3_column_double(songStmt, 2);
        song.initialState = col_str(songStmt, 3);

        double tempo = sqlite3_column_double(songStmt, 4);
        int tsNum = sqlite3_column_int(songStmt, 5);
        int tsDen = sqlite3_column_int(songStmt, 6);
        if (tempo > 0) song.tempoEvents.push_back({ 0.0, tempo });
        if (tsNum > 0 && tsDen > 0) song.timeSigEvents.push_back({ 0.0, tsNum, tsDen });

        // Tracks
        auto* ts = prepare("SELECT id, name, plugin_id, preset_id, output_gain, midi_enabled, position, processor_state, processor_state_hash, source_type, channel_mode, input_channel_start, input_channel_count, audio_enabled, color, output_target FROM tracks WHERE song_id = ? ORDER BY position");
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
            auto srcType = col_str(ts, 9);
            t.sourceType = (srcType == "audio_input") ? TrackSourceType::AudioInput
                         : (srcType == "action") ? TrackSourceType::Action
                         : TrackSourceType::Instrument;
            auto chMode = col_str(ts, 10);
            t.channelMode = (chMode == "mono") ? ChannelMode::Mono : ChannelMode::Stereo;
            t.inputChannelStart = sqlite3_column_int(ts, 11);
            t.inputChannelCount = sqlite3_column_int(ts, 12);
            t.audioEnabled = sqlite3_column_int(ts, 13) != 0;
            t.color = (uint32_t)sqlite3_column_int(ts, 14);
            t.outputTarget = col_str(ts, 15);

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

            // Regions for this track
            auto* rs = prepare("SELECT id, type, name, start_beat, length_beats, active_take_id, muted, quantize FROM regions WHERE track_id = ? ORDER BY start_beat");
            sqlite3_bind_text(rs, 1, t.id.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(rs) == SQLITE_ROW) {
                RegionState r;
                r.id = col_str(rs, 0);
                r.type = col_str(rs, 1);
                r.name = col_str(rs, 2);
                r.startBeat = sqlite3_column_double(rs, 3);
                r.lengthBeats = sqlite3_column_double(rs, 4);
                r.activeTakeId = col_str(rs, 5);
                r.muted = sqlite3_column_int(rs, 6) != 0;
                r.quantize = sqlite3_column_double(rs, 7);

                // Takes for this region
                auto* tks = prepare("SELECT id, name FROM takes WHERE region_id = ?");
                sqlite3_bind_text(tks, 1, r.id.c_str(), -1, SQLITE_TRANSIENT);
                while (sqlite3_step(tks) == SQLITE_ROW) {
                    TakeState take;
                    take.id = col_str(tks, 0);
                    take.name = col_str(tks, 1);

                    // Events for this take
                    auto* es = prepare("SELECT beat_offset, status, channel, data1, data2 FROM take_events WHERE take_id = ? ORDER BY beat_offset");
                    sqlite3_bind_text(es, 1, take.id.c_str(), -1, SQLITE_TRANSIENT);
                    while (sqlite3_step(es) == SQLITE_ROW) {
                        MidiEventState e;
                        e.beatOffset = sqlite3_column_double(es, 0);
                        e.status = sqlite3_column_int(es, 1);
                        e.channel = sqlite3_column_int(es, 2);
                        e.data1 = sqlite3_column_int(es, 3);
                        e.data2 = sqlite3_column_int(es, 4);
                        take.events.push_back(e);
                    }
                    sqlite3_finalize(es);

                    r.takes.push_back(std::move(take));
                }
                sqlite3_finalize(tks);

                t.regions.push_back(std::move(r));
            }
            sqlite3_finalize(rs);

            song.tracks.push_back(std::move(t));
        }
        sqlite3_finalize(ts);

        // Busses
        auto* bs = prepare("SELECT id, name, output_gain, position, output_target FROM busses WHERE song_id = ? ORDER BY position");
        sqlite3_bind_text(bs, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(bs) == SQLITE_ROW) {
            BusState b;
            b.id = col_str(bs, 0);
            b.name = col_str(bs, 1);
            b.outputGain = (float)sqlite3_column_double(bs, 2);
            b.position = sqlite3_column_int(bs, 3);
            b.outputTarget = col_str(bs, 4);

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

        // Song-device associations
        auto* sd = prepare("SELECT device_id FROM song_devices WHERE song_id = ?");
        sqlite3_bind_text(sd, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(sd) == SQLITE_ROW)
            song.deviceIds.push_back(col_str(sd, 0));
        sqlite3_finalize(sd);

        // Song-scoped bindings (includes score steps via isScoreStep flag)
        auto* bi = prepare("SELECT id, device_id, control_type, channel, number, action_id, args, description, is_score_step, score_position FROM bindings WHERE song_id = ?");
        sqlite3_bind_text(bi, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(bi) == SQLITE_ROW) {
            song.bindings.push_back({
                col_str(bi, 0), song.id, col_str(bi, 1), col_str(bi, 2),
                sqlite3_column_int(bi, 3), sqlite3_column_int(bi, 4),
                col_str(bi, 5), col_str(bi, 6), col_str(bi, 7),
                sqlite3_column_int(bi, 8) != 0, sqlite3_column_int(bi, 9)
            });
        }
        sqlite3_finalize(bi);

        out.songs.push_back(std::move(song));

        // Action events for action tracks (after song is pushed so tracks exist)
        auto& savedSong = out.songs.back();
        for (auto& t : savedSong.tracks) {
            if (t.sourceType != TrackSourceType::Action) continue;
            auto* ae = prepare("SELECT id, beat, action_id, args_json FROM action_events WHERE track_id = ? ORDER BY beat");
            sqlite3_bind_text(ae, 1, t.id.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(ae) == SQLITE_ROW) {
                ActionEventData ev;
                ev.id = col_str(ae, 0);
                ev.beat = sqlite3_column_double(ae, 1);
                ev.actionId = col_str(ae, 2);
                ev.argsJson = col_str(ae, 3);
                t.actionData.push_back(std::move(ev));
            }
            sqlite3_finalize(ae);
        }
    }
    sqlite3_finalize(songStmt);

    // Global bindings
    auto* gb = prepare("SELECT id, device_id, control_type, channel, number, action_id, args, description, is_score_step, score_position FROM bindings WHERE song_id IS NULL");
    while (sqlite3_step(gb) == SQLITE_ROW) {
        out.globalBindings.push_back({
            col_str(gb, 0), "", col_str(gb, 1), col_str(gb, 2),
            sqlite3_column_int(gb, 3), sqlite3_column_int(gb, 4),
            col_str(gb, 5), col_str(gb, 6), col_str(gb, 7),
            sqlite3_column_int(gb, 8) != 0, sqlite3_column_int(gb, 9)
        });
    }
    sqlite3_finalize(gb);
}

void PersistenceLayer::readDevices(AppState& out) {
    auto* ds = prepare("SELECT id, name, midi_port_name FROM devices");
    while (sqlite3_step(ds) == SQLITE_ROW) {
        DeviceState device;
        device.id = col_str(ds, 0);
        device.name = col_str(ds, 1);
        device.midiPortName = col_str(ds, 2);

        // Controls for this device
        auto* cs = prepare("SELECT name, control_type, channel, number, group_name FROM device_controls WHERE device_id = ? ORDER BY position");
        sqlite3_bind_text(cs, 1, device.id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(cs) == SQLITE_ROW) {
            device.controls.push_back({
                col_str(cs, 0), col_str(cs, 1),
                sqlite3_column_int(cs, 2), sqlite3_column_int(cs, 3),
                col_str(cs, 4)
            });
        }
        sqlite3_finalize(cs);

        out.devices.push_back(std::move(device));
    }
    sqlite3_finalize(ds);
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
    // Backup DB before save
    if (!dbPath.empty()) {
        auto src = juce::File(dbPath);
        auto bak = src.getSiblingFile(src.getFileNameWithoutExtension() + ".bak.db");
        src.copyFileTo(bak);
    }

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
        auto* stmt = prepare("INSERT OR REPLACE INTO actions (id, name, label, param_schema, lua_code, song_id) VALUES (?, ?, ?, ?, ?, ?)");
        sqlite3_bind_text(stmt, 1, a.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, a.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, a.label.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, a.paramSchema.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, a.luaCode.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, a.songId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void PersistenceLayer::saveSongs(const StateAPI& state) {
    // Delete all existing data
    exec("DELETE FROM take_events");
    exec("DELETE FROM takes");
    exec("DELETE FROM regions");
    exec("DELETE FROM effects");
    exec("DELETE FROM sends");
    exec("DELETE FROM song_devices");
    exec("DELETE FROM action_events");
    exec("DELETE FROM songs");  // CASCADE clears tracks, busses, bindings
    exec("DELETE FROM device_controls");
    exec("DELETE FROM devices");

    // Write devices
    for (auto& device : state.appState().devices) {
        auto* ds = prepare("INSERT INTO devices (id, name, midi_port_name) VALUES (?, ?, ?)");
        sqlite3_bind_text(ds, 1, device.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ds, 2, device.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ds, 3, device.midiPortName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(ds);
        sqlite3_finalize(ds);

        for (int i = 0; i < (int)device.controls.size(); ++i) {
            auto& ctrl = device.controls[i];
            auto ctrlId = StateAPI::generateId();
            auto* cs = prepare("INSERT INTO device_controls (id, device_id, name, control_type, channel, number, group_name, position) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
            sqlite3_bind_text(cs, 1, ctrlId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(cs, 2, device.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(cs, 3, ctrl.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(cs, 4, ctrl.controlType.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(cs, 5, ctrl.channel);
            sqlite3_bind_int(cs, 6, ctrl.number);
            if (!ctrl.group.empty())
                sqlite3_bind_text(cs, 7, ctrl.group.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(cs, 7);
            sqlite3_bind_int(cs, 8, i);
            sqlite3_step(cs);
            sqlite3_finalize(cs);
        }
    }
    // Delete global bindings separately (not cascaded)
    exec("DELETE FROM bindings WHERE song_id IS NULL");

    for (auto& song : state.allSongs()) {
        // Song
        double songTempo = song.tempoEvents.empty() ? 120.0 : song.tempoEvents[0].bpm;
        int songTsNum = song.timeSigEvents.empty() ? 4 : song.timeSigEvents[0].numerator;
        int songTsDen = song.timeSigEvents.empty() ? 4 : song.timeSigEvents[0].denominator;

        auto* stmt = prepare("INSERT INTO songs (id, name, master_gain, initial_state, tempo, time_sig_num, time_sig_den) VALUES (?, ?, ?, ?, ?, ?, ?)");
        sqlite3_bind_text(stmt, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, song.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, song.masterGain);
        if (!song.initialState.empty())
            sqlite3_bind_text(stmt, 4, song.initialState.c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 4);
        sqlite3_bind_double(stmt, 5, songTempo);
        sqlite3_bind_int(stmt, 6, songTsNum);
        sqlite3_bind_int(stmt, 7, songTsDen);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        // Busses (before tracks, since sends reference busses)
        for (auto& b : song.busses) {
            auto* bs = prepare("INSERT INTO busses (id, song_id, name, output_gain, position, output_target) VALUES (?, ?, ?, ?, ?, ?)");
            sqlite3_bind_text(bs, 1, b.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(bs, 2, song.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(bs, 3, b.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(bs, 4, b.outputGain);
            sqlite3_bind_int(bs, 5, b.position);
            sqlite3_bind_text(bs, 6, b.outputTarget.c_str(), -1, SQLITE_TRANSIENT);
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

        // Song-device associations
        for (auto& devId : song.deviceIds) {
            auto* sd = prepare("INSERT INTO song_devices (song_id, device_id) VALUES (?, ?)");
            sqlite3_bind_text(sd, 1, song.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(sd, 2, devId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(sd);
            sqlite3_finalize(sd);
        }

        // Tracks (after busses, since sends reference bus IDs)
        for (auto& t : song.tracks) {
            auto* ts = prepare("INSERT INTO tracks (id, song_id, name, plugin_id, preset_id, output_gain, midi_enabled, position, processor_state, processor_state_hash, source_type, channel_mode, input_channel_start, input_channel_count, audio_enabled, color, output_target) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
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
            auto srcType = (t.sourceType == TrackSourceType::Action) ? "action"
                         : (t.sourceType == TrackSourceType::AudioInput) ? "audio_input"
                         : "instrument";
            sqlite3_bind_text(ts, 11, srcType, -1, SQLITE_TRANSIENT);
            auto chMode = (t.channelMode == ChannelMode::Mono) ? "mono" : "stereo";
            sqlite3_bind_text(ts, 12, chMode, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ts, 13, t.inputChannelStart);
            sqlite3_bind_int(ts, 14, t.inputChannelCount);
            sqlite3_bind_int(ts, 15, t.audioEnabled ? 1 : 0);
            sqlite3_bind_int(ts, 16, (int)t.color);
            sqlite3_bind_text(ts, 17, t.outputTarget.c_str(), -1, SQLITE_TRANSIENT);
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

            // Regions and takes
            for (auto& r : t.regions) {
                auto* rs = prepare(
                    "INSERT INTO regions (id, track_id, type, name, start_beat, length_beats, active_take_id, muted, quantize) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
                sqlite3_bind_text(rs, 1, r.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(rs, 2, t.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(rs, 3, r.type.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(rs, 4, r.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(rs, 5, r.startBeat);
                sqlite3_bind_double(rs, 6, r.lengthBeats);
                sqlite3_bind_text(rs, 7, r.activeTakeId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(rs, 8, r.muted ? 1 : 0);
                sqlite3_bind_double(rs, 9, r.quantize);
                sqlite3_step(rs);
                sqlite3_finalize(rs);

                for (auto& take : r.takes) {
                    auto* ts2 = prepare(
                        "INSERT INTO takes (id, region_id, name) VALUES (?, ?, ?)");
                    sqlite3_bind_text(ts2, 1, take.id.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(ts2, 2, r.id.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(ts2, 3, take.name.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(ts2);
                    sqlite3_finalize(ts2);

                    for (auto& e : take.events) {
                        auto* es = prepare(
                            "INSERT INTO take_events (take_id, beat_offset, status, channel, data1, data2) "
                            "VALUES (?, ?, ?, ?, ?, ?)");
                        sqlite3_bind_text(es, 1, take.id.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_double(es, 2, e.beatOffset);
                        sqlite3_bind_int(es, 3, e.status);
                        sqlite3_bind_int(es, 4, e.channel);
                        sqlite3_bind_int(es, 5, e.data1);
                        sqlite3_bind_int(es, 6, e.data2);
                        sqlite3_step(es);
                        sqlite3_finalize(es);
                    }
                }
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
            auto* bs = prepare("INSERT INTO bindings (id, song_id, device_id, control_type, channel, number, action_id, args, description, is_score_step, score_position) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            sqlite3_bind_text(bs, 1, bind.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(bs, 2, song.id.c_str(), -1, SQLITE_TRANSIENT);
            if (!bind.deviceId.empty())
                sqlite3_bind_text(bs, 3, bind.deviceId.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(bs, 3);
            sqlite3_bind_text(bs, 4, bind.controlType.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(bs, 5, bind.channel);
            sqlite3_bind_int(bs, 6, bind.number);
            sqlite3_bind_text(bs, 7, bind.actionId.c_str(), -1, SQLITE_TRANSIENT);
            if (!bind.args.empty())
                sqlite3_bind_text(bs, 8, bind.args.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(bs, 8);
            if (!bind.description.empty())
                sqlite3_bind_text(bs, 9, bind.description.c_str(), -1, SQLITE_TRANSIENT);
            else sqlite3_bind_null(bs, 9);
            sqlite3_bind_int(bs, 10, bind.isScoreStep ? 1 : 0);
            sqlite3_bind_int(bs, 11, bind.scorePosition);
            sqlite3_step(bs);
            sqlite3_finalize(bs);
        }
        // Action events (on action tracks)
        for (auto& t : song.tracks) {
            if (t.sourceType != TrackSourceType::Action) continue;
            for (auto& ev : t.actionData) {
                auto* aes = prepare("INSERT INTO action_events (id, track_id, beat, action_id, args_json) VALUES (?, ?, ?, ?, ?)");
                sqlite3_bind_text(aes, 1, ev.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(aes, 2, t.id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(aes, 3, ev.beat);
                sqlite3_bind_text(aes, 4, ev.actionId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(aes, 5, ev.argsJson.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(aes);
                sqlite3_finalize(aes);
            }
        }
    }

    // Global bindings
    for (auto& bind : state.appState().globalBindings) {
        auto* bs = prepare("INSERT INTO bindings (id, song_id, device_id, control_type, channel, number, action_id, args, description, is_score_step, score_position) VALUES (?, NULL, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        sqlite3_bind_text(bs, 1, bind.id.c_str(), -1, SQLITE_TRANSIENT);
        if (!bind.deviceId.empty())
            sqlite3_bind_text(bs, 2, bind.deviceId.c_str(), -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(bs, 2);
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

