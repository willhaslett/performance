#pragma once
#include "state/StateModel.h"
#include "state/StateEvents.h"
#include "state/UndoHistory.h"
#include <string>
#include <vector>

// In-memory state store — the single source of truth while the app is running.
// All consumers (GUI, Lua, Claude, MIDI bindings, EngineSync) read and write through this.
// No JUCE dependency. No SQLite. Pure C++ state with observable mutations.

class StateAPI {
public:
    StateAPI();

    // --- Song ---
    SongId createSong(const std::string& name);
    void deleteSong(const SongId& id);
    SongState* currentSong();
    const SongState* currentSong() const;
    SongState* findSong(const SongId& id);
    const SongState* findSong(const SongId& id) const;
    const std::vector<SongState>& allSongs() const;
    void setCurrentSong(const SongId& songId);
    void setMasterGain(float gain);
    float getMasterGain() const;
    void setSongTempo(double bpm);
    double getSongTempo() const;
    void setSongTimeSignature(int numerator, int denominator);
    std::pair<int,int> getSongTimeSignature() const;
    void setMasterAudioEnabled(bool enabled);
    bool isMasterAudioEnabled() const;
    std::string getMasterOutputId() const;

    // --- Tracks ---
    TrackId createTrack(const std::string& name);  // virtual instrument track
    TrackId createAudioInputTrack(const std::string& name, int inputChannelStart,
                                   int inputChannelCount);
    TrackId createActionTrack(const std::string& name);
    void removeTrack(const TrackId& id);
    void renameTrack(const TrackId& id, const std::string& name);
    void moveTrack(const TrackId& id, int newPosition);  // reorder within song
    void setTrackGain(const TrackId& id, float gain);
    float getTrackGain(const TrackId& id) const;
    void setTrackMidiEnabled(const TrackId& id, bool enabled);
    bool isTrackMidiEnabled(const TrackId& id) const;
    void setTrackAudioEnabled(const TrackId& id, bool enabled);
    bool isTrackAudioEnabled(const TrackId& id) const;
    void setTrackArmed(const TrackId& id, bool armed);
    bool isTrackArmed(const TrackId& id) const;
    void setTrackInputMonitoring(const TrackId& id, bool enabled);
    bool isTrackInputMonitoring(const TrackId& id) const;
    void setTrackMuted(const TrackId& id, bool muted);
    bool isTrackMuted(const TrackId& id) const;
    void setTrackSoloed(const TrackId& id, bool soloed);
    bool isTrackSoloed(const TrackId& id) const;
    bool isAnySoloed() const;  // true if any track in current song is soloed
    void setTrackPlugin(const TrackId& id, const PluginId& pluginId,
                        const PresetId& presetId = {});
    void clearTrackPlugin(const TrackId& id);
    void setTrackPresetId(const TrackId& id, const PresetId& presetId);
    void setTrackInputChannels(const TrackId& id, int start, int count);
    void setTrackOutputTarget(const TrackId& id, const std::string& target);
    std::string getTrackOutputTarget(const TrackId& id) const;
    void setTrackInstrumentLoadStatus(const TrackId& id, LoadStatus status);
    TrackState* findTrack(const TrackId& id);
    const TrackState* findTrack(const TrackId& id) const;

    // --- Busses ---
    BusId createBus(const std::string& name);
    void removeBus(const BusId& id);
    void renameBus(const BusId& id, const std::string& name);
    void setBusGain(const BusId& id, float gain);
    float getBusGain(const BusId& id) const;
    void setBusAudioEnabled(const BusId& id, bool enabled);
    bool isBusAudioEnabled(const BusId& id) const;
    void setBusOutputTarget(const BusId& id, const std::string& target);
    std::string getBusOutputTarget(const BusId& id) const;
    BusState* findBus(const BusId& id);
    const BusState* findBus(const BusId& id) const;

    // --- Effects (on tracks, busses, or master output) ---
    EffectId addEffect(const std::string& parentId, const std::string& name,
                          const PluginId& pluginId);  // parentId is raw string: could be trackId.str(), busId.str(), or master-output id
    void removeEffect(const EffectId& effectId);
    void setEffectLoadStatus(const EffectId& effectId, LoadStatus status);
    const EffectState* findEffect(const EffectId& effectId) const;
    void setEffectPresetId(const EffectId& effectId, const PresetId& presetId);

    // --- Sends ---
    std::string addSend(const TrackId& trackId, const BusId& busId, float gain = 1.0f);
    void removeSend(const std::string& sendId);
    void setSendGain(const std::string& sendId, float gain);
    void setSendGainByBus(const TrackId& trackId, const BusId& busId, float gain);

    // --- Bindings (songId empty = global, deviceId empty = any device) ---
    std::string addBinding(const std::string& songId, const std::string& controlType,
                           int channel, int number, const std::string& actionId,
                           const std::string& args = "[]", const std::string& description = "",
                           const std::string& deviceId = "");
    std::string addGlobalBinding(const std::string& controlType, int channel, int number,
                                  const std::string& actionId, const std::string& args = "[]",
                                  const std::string& description = "",
                                  const std::string& deviceId = "");
    void removeBinding(const std::string& id);
    std::vector<BindingState> bindingsForSong(const std::string& songId) const;
    std::vector<BindingState> globalBindings() const;
    std::vector<BindingState> effectiveBindings() const;  // global + current song (song wins on conflict)

    // --- Action Events (beat-triggered actions on the timeline) ---
    std::string addActionEvent(double beat, const std::string& actionId,
                               const std::string& argsJson = "[]");
    void removeActionEvent(const std::string& id);
    void setActionEventBeat(const std::string& id, double beat);
    std::vector<SongState::ActionEvent>& actionEvents();

    // Score — ordered subset of song bindings where isScoreStep == true
    std::vector<BindingState> scoreSteps() const;  // sorted by scorePosition
    void setBindingAsScoreStep(const std::string& bindingId, int position);
    void clearScoreStep(const std::string& bindingId);

    // --- Devices ---
    std::string registerDevice(const std::string& name, const std::string& midiPortName);
    void removeDevice(const std::string& id);
    DeviceState* findDevice(const std::string& id);
    const DeviceState* findDevice(const std::string& id) const;
    DeviceState* findDeviceByPortName(const std::string& portName);
    const std::vector<DeviceState>& allDevices() const;
    void renameDevice(const std::string& id, const std::string& name);
    std::string addDeviceControl(const std::string& deviceId, const std::string& name,
                                 const std::string& controlType, int channel, int number,
                                 const std::string& group = "");
    void removeDeviceControl(const std::string& deviceId, int index);
    void renameDeviceControl(const std::string& deviceId, int index, const std::string& name);
    void setDeviceControlGroup(const std::string& deviceId, int index, const std::string& group);

    // Song-device association
    void addDeviceToSong(const std::string& songId, const std::string& deviceId);
    void removeDeviceFromSong(const std::string& songId, const std::string& deviceId);
    std::vector<std::string> devicesForSong(const std::string& songId) const;

    // --- Catalog: Plugins ---
    PluginId registerPlugin(const std::string& name, const std::string& manufacturer,
                            const std::string& formatId, bool isInstrument = false);
    const PluginInfo* findPluginByName(const std::string& name) const;
    const PluginInfo* findPluginById(const PluginId& id) const;
    const std::vector<PluginInfo>& allPlugins() const;

    // --- Catalog: Presets ---
    PresetId createPreset(const PluginId& pluginId, const std::string& name,
                          const std::string& statePath, PresetKind kind = PresetKind::Instrument);
    const PresetInfo* findPreset(const PluginId& pluginId, const std::string& name) const;
    const PresetInfo* findPresetById(const PresetId& id) const;
    std::vector<const PresetInfo*> presetsForPlugin(const PluginId& pluginId) const;

    // --- Catalog: Actions ---
    std::string registerAction(const std::string& name, const std::string& label = "",
                               const std::string& paramSchema = "",
                               int durationParamIndex = -1);
    std::string createCustomAction(const std::string& name, const std::string& label,
                                    const std::string& luaCode, const std::string& songId = "");
    void removeAction(const std::string& id);
    const ActionInfo* findActionByName(const std::string& name) const;
    const ActionInfo* findActionById(const std::string& id) const;
    const std::vector<ActionInfo>& allActions() const;

    // --- Selection ---
    void selectTrack(const TrackId& trackId, bool addToSelection = false);
    void selectBus(const BusId& busId, bool addToSelection = false);
    void clearSelection();
    std::vector<TrackId> selectedTrackIds() const;
    std::vector<BusId> selectedBusIds() const;

    // --- Config ---
    void setConfig(const std::string& key, const std::string& value);
    std::string getConfig(const std::string& key, const std::string& defaultValue = "") const;

    // --- Name resolution (Lua convenience) ---
    TrackId findTrackIdByName(const std::string& name) const;
    BusId findBusIdByName(const std::string& name) const;

    // --- Query helpers ---
    struct TrackInfo { TrackId id; std::string name; };
    struct BusInfo { BusId id; std::string name; };
    struct EffectSlotInfo { EffectId effectId; std::string pluginName; };
    struct TrackSendInfo { std::string busName; BusId busId; float gain; };

    std::vector<TrackInfo> listTracks() const;
    std::vector<BusInfo> listBusses() const;
    std::string getTrackPluginName(const TrackId& trackId) const;
    std::vector<EffectSlotInfo> getTrackEffects(const TrackId& trackId) const;
    std::vector<EffectSlotInfo> getBusEffects(const BusId& busId) const;
    std::vector<EffectSlotInfo> getMasterEffects() const;
    std::vector<TrackSendInfo> getTrackSends(const TrackId& trackId) const;

    // --- Events ---
    StateEventBus& events();

    // --- Dirty tracking ---
    bool isDirty() const;
    void markDirty();
    void clearDirty();

    // --- Direct access to full state (for persistence layer) ---
    const AppState& appState() const { return state; }

    // Atomically replace the entire state (used by persistence load).
    // Fires a single Config "current_song_id" event so EngineSync rebuilds.
    void replaceState(AppState&& newState);

    // --- Undo/Redo ---
    void pushUndo();  // snapshot current state (call before mutations)
    void beginTransaction();  // begin a grouped operation (one undo step)
    void endTransaction();    // end grouped operation
    bool undo();      // returns true if state was restored
    bool redo();
    bool canUndo() const { return undoHistory.canUndo(); }
    bool canRedo() const { return undoHistory.canRedo(); }
    void suspendUndo() { undoHistory.suspend(); }
    void resumeUndo() { undoHistory.resume(); }
    UndoHistory& getUndoHistory() { return undoHistory; }

    // --- UUID generation ---
    static std::string generateId();

private:
    AppState state;
    StateEventBus eventBus;
    UndoHistory undoHistory;
    bool dirty = false;
    bool inTransaction = false;

    // Asserting accessors — crash on invariant violation (programming error).
    // Use inside StateAPI methods where the entity must exist.
    SongState& song();
    const SongState& song() const;
    TrackState& track(const TrackId& id);
    const TrackState& track(const TrackId& id) const;
    BusState& bus(const BusId& id);
    const BusState& bus(const BusId& id) const;
    DeviceState& device(const std::string& id);

    // Find the effects vector that contains effectId, and optionally the parent ID
    std::vector<EffectState>* findEffectList(const EffectId& effectId, std::string* outParentId = nullptr);
    // Find the sends vector that contains sendId
    std::vector<SendState>* findSendList(const std::string& sendId, std::string* outTrackId = nullptr);
};
