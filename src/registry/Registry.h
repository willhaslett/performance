#pragma once
#include <juce_core/juce_core.h>
#include "registry/RegistryEvents.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <map>
#include <optional>

// Generic entity: type + map of field names to string values
struct Entity {
    std::string id;
    std::string type;
    std::map<std::string, std::string> fields;

    std::string get(const std::string& key, const std::string& defaultVal = "") const {
        auto it = fields.find(key);
        return it != fields.end() ? it->second : defaultVal;
    }
};

class Registry {
public:
    Registry();
    ~Registry();

    void open(const std::string& dbPath);
    void close();

    // --- Generic CRUD ---
    std::string create(const std::string& type, const std::map<std::string, std::string>& fields);
    std::optional<Entity> get(const std::string& id);
    std::vector<Entity> list(const std::string& type,
                              const std::map<std::string, std::string>& filters = {});
    void update(const std::string& id, const std::map<std::string, std::string>& fields);
    void remove(const std::string& id);

    // --- Plugins ---
    struct Plugin {
        std::string id;
        std::string name;
        std::string manufacturer;
        std::string formatId;  // AU identifier string
    };
    std::string registerPlugin(const std::string& name, const std::string& manufacturer,
                                const std::string& formatId);
    std::optional<Plugin> findPluginByName(const std::string& name) const;
    std::optional<Plugin> findPluginById(const std::string& id) const;
    std::vector<Plugin> allPlugins() const;

    // --- Snapshots ---
    struct Snapshot {
        std::string id;
        std::string pluginId;
        std::string name;
        std::string statePath;
    };
    std::string createSnapshot(const std::string& pluginId, const std::string& name,
                                const std::string& statePath);
    std::optional<Snapshot> findSnapshot(const std::string& pluginId, const std::string& name) const;
    std::optional<Snapshot> findSnapshotById(const std::string& id) const;
    std::vector<Snapshot> snapshotsForPlugin(const std::string& pluginId) const;

    // --- Songs ---
    struct Song {
        std::string id;
        std::string name;
    };
    std::string createSong(const std::string& name);
    std::optional<Song> findSongByName(const std::string& name) const;
    std::optional<Song> findSongById(const std::string& id) const;
    std::vector<Song> allSongs() const;
    void deleteSong(const std::string& id);
    void setMasterGain(const std::string& songId, float gain);
    float getMasterGain(const std::string& songId) const;

    // --- Tracks ---
    struct Track {
        std::string id;
        std::string songId;
        std::string name;
        std::string pluginId;
        std::string snapshotId;  // may be empty
        float outputGain = 1.0f;
        bool midiEnabled = true;
        int position = 0;
    };
    std::string createTrack(const std::string& songId, const std::string& name,
                             const std::string& pluginId, const std::string& snapshotId = "",
                             float outputGain = 1.0f, bool midiEnabled = true);
    std::vector<Track> tracksForSong(const std::string& songId) const;
    void deleteTrack(const std::string& id);
    void updateTrack(const Track& track);

    // --- Busses ---
    struct Bus {
        std::string id;
        std::string songId;
        std::string name;
        float outputGain = 1.0f;
        int position = 0;
    };
    std::string createBus(const std::string& songId, const std::string& name,
                           float outputGain = 1.0f);
    std::vector<Bus> bussesForSong(const std::string& songId) const;
    void deleteBus(const std::string& id);

    // --- Effects ---
    struct Effect {
        std::string id;
        std::string parentId;
        std::string parentType;  // "track" or "bus"
        std::string name;
        std::string pluginId;
        std::string snapshotId;
        int position = 0;
    };
    std::string createEffect(const std::string& parentId, const std::string& parentType,
                              const std::string& name, const std::string& pluginId,
                              const std::string& snapshotId = "");
    std::vector<Effect> effectsForParent(const std::string& parentId) const;

    // --- Sends ---
    struct Send {
        std::string id;
        std::string trackId;
        std::string busId;
        float gain = 1.0f;
    };
    std::string createSend(const std::string& trackId, const std::string& busId,
                            float gain = 1.0f);
    std::vector<Send> sendsForTrack(const std::string& trackId) const;

    // --- Actions ---
    struct Action {
        std::string id;
        std::string name;
        std::string label;        // user-friendly display name
        std::string paramSchema;  // JSON
    };
    std::string registerAction(const std::string& name, const std::string& label = "",
                                const std::string& paramSchema = "");
    std::optional<Action> findActionByName(const std::string& name) const;
    std::optional<Action> findActionById(const std::string& id) const;
    std::vector<Action> allActions() const;

    // --- Bindings ---
    struct Binding {
        std::string id;
        std::string songId;
        std::string controlType;
        int channel = 0;
        int number = 0;
        std::string actionId;
        std::string args;  // JSON array
        std::string description;
    };
    std::string createBinding(const std::string& songId, const std::string& controlType,
                               int channel, int number, const std::string& actionId,
                               const std::string& args = "[]", const std::string& description = "");
    std::vector<Binding> bindingsForSong(const std::string& songId) const;
    void deleteBinding(const std::string& id);

    // --- UUID ---
    static std::string generateId();

    // --- Events ---
    RegistryEventBus& events() { return eventBus; }

private:
    sqlite3* db = nullptr;
    RegistryEventBus eventBus;

    void createSchema();
    void exec(const std::string& sql) const;
    sqlite3_stmt* prepare(const std::string& sql) const;
};
