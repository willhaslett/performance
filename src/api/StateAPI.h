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
    std::string createSong(const std::string& name);
    void deleteSong(const std::string& id);
    SongState* currentSong();
    const SongState* currentSong() const;
    SongState* findSong(const std::string& id);
    const SongState* findSong(const std::string& id) const;
    const std::vector<SongState>& allSongs() const;
    void setCurrentSong(const std::string& songId);
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
    std::string createTrack(const std::string& name);  // virtual instrument track
    std::string createAudioInputTrack(const std::string& name, int inputChannelStart,
                                       int inputChannelCount);
    std::string createActionTrack(const std::string& name);
    void removeTrack(const std::string& id);
    void renameTrack(const std::string& id, const std::string& name);
    void moveTrack(const std::string& id, int newPosition);  // reorder within song
    void setTrackGain(const std::string& id, float gain);
    float getTrackGain(const std::string& id) const;
    void setTrackMidiEnabled(const std::string& id, bool enabled);
    bool isTrackMidiEnabled(const std::string& id) const;
    void setTrackAudioEnabled(const std::string& id, bool enabled);
    bool isTrackAudioEnabled(const std::string& id) const;
    void setTrackArmed(const std::string& id, bool armed);
    bool isTrackArmed(const std::string& id) const;
    void setTrackInputMonitoring(const std::string& id, bool enabled);
    bool isTrackInputMonitoring(const std::string& id) const;
    void setTrackPlugin(const std::string& id, const std::string& pluginId,
                        const std::string& presetId = "");
    void clearTrackPlugin(const std::string& id);
    void setTrackPresetId(const std::string& id, const std::string& presetId);
    void setTrackInputChannels(const std::string& id, int start, int count);
    void setTrackOutputTarget(const std::string& id, const std::string& target);
    std::string getTrackOutputTarget(const std::string& id) const;
    void setTrackInstrumentLoadStatus(const std::string& id, LoadStatus status);
    TrackState* findTrack(const std::string& id);
    const TrackState* findTrack(const std::string& id) const;

    // --- Busses ---
    std::string createBus(const std::string& name);
    void removeBus(const std::string& id);
    void renameBus(const std::string& id, const std::string& name);
    void setBusGain(const std::string& id, float gain);
    float getBusGain(const std::string& id) const;
    void setBusAudioEnabled(const std::string& id, bool enabled);
    bool isBusAudioEnabled(const std::string& id) const;
    void setBusOutputTarget(const std::string& id, const std::string& target);
    std::string getBusOutputTarget(const std::string& id) const;
    BusState* findBus(const std::string& id);
    const BusState* findBus(const std::string& id) const;

    // --- Effects (on tracks, busses, or master output) ---
    std::string addEffect(const std::string& parentId, const std::string& name,
                          const std::string& pluginId);
    void removeEffect(const std::string& effectId);
    void setEffectLoadStatus(const std::string& effectId, LoadStatus status);
    const EffectState* findEffect(const std::string& effectId) const;
    void setEffectPresetId(const std::string& effectId, const std::string& presetId);

    // --- Sends ---
    std::string addSend(const std::string& trackId, const std::string& busId, float gain = 1.0f);
    void removeSend(const std::string& sendId);
    void setSendGain(const std::string& sendId, float gain);
    void setSendGainByBus(const std::string& trackId, const std::string& busId, float gain);

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
    std::string registerPlugin(const std::string& name, const std::string& manufacturer,
                               const std::string& formatId, bool isInstrument = false);
    const PluginInfo* findPluginByName(const std::string& name) const;
    const PluginInfo* findPluginById(const std::string& id) const;
    const std::vector<PluginInfo>& allPlugins() const;

    // --- Catalog: Presets ---
    std::string createPreset(const std::string& pluginId, const std::string& name,
                             const std::string& statePath, PresetKind kind = PresetKind::Instrument);
    const PresetInfo* findPreset(const std::string& pluginId, const std::string& name) const;
    const PresetInfo* findPresetById(const std::string& id) const;
    std::vector<const PresetInfo*> presetsForPlugin(const std::string& pluginId) const;

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
    void selectTrack(const std::string& trackId, bool addToSelection = false);
    void selectBus(const std::string& busId, bool addToSelection = false);
    void clearSelection();
    std::vector<std::string> selectedTrackIds() const;
    std::vector<std::string> selectedBusIds() const;

    // --- Config ---
    void setConfig(const std::string& key, const std::string& value);
    std::string getConfig(const std::string& key, const std::string& defaultValue = "") const;

    // --- Name resolution (Lua convenience) ---
    std::string findTrackIdByName(const std::string& name) const;
    std::string findBusIdByName(const std::string& name) const;

    // --- Query helpers ---
    struct TrackInfo { std::string id; std::string name; };
    struct BusInfo { std::string id; std::string name; };
    struct EffectSlotInfo { std::string effectId; std::string pluginName; };
    struct TrackSendInfo { std::string busName; std::string busId; float gain; };

    std::vector<TrackInfo> listTracks() const;
    std::vector<BusInfo> listBusses() const;
    std::string getTrackPluginName(const std::string& trackId) const;
    std::vector<EffectSlotInfo> getTrackEffects(const std::string& trackId) const;
    std::vector<EffectSlotInfo> getBusEffects(const std::string& busId) const;
    std::vector<EffectSlotInfo> getMasterEffects() const;
    std::vector<TrackSendInfo> getTrackSends(const std::string& trackId) const;

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
    TrackState& track(const std::string& id);
    const TrackState& track(const std::string& id) const;
    BusState& bus(const std::string& id);
    const BusState& bus(const std::string& id) const;
    DeviceState& device(const std::string& id);

    // Find the effects vector that contains effectId, and optionally the parent ID
    std::vector<EffectState>* findEffectList(const std::string& effectId, std::string* outParentId = nullptr);
    // Find the sends vector that contains sendId
    std::vector<SendState>* findSendList(const std::string& sendId, std::string* outTrackId = nullptr);
};
