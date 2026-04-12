#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "state/StateEvents.h"
#include "state/StateModel.h"
#include "daw/SequencerAPI.h"
#include "daw/Arrangement.h"
#include "engine/AudioRecordFIFO.h"
#include "engine/AudioWriterThread.h"
#include <functional>
#include <map>
#include <memory>
#include <string>

class AudioEngine;
class AutomationEngine;
class EngineAPI;
class EngineSync;
class MIDIEngine;
class PersistenceLayer;
class StateAPI;
class SongRuntime;

// Lifecycle owner and orchestrator. Owns all subsystems, wires them together.
// Consumers get StateAPI& and EngineAPI& from this — they never interact
// with the coordinator directly except for lifecycle and cross-cutting operations.

class PerformanceCoordinator : private juce::Timer {
public:
    PerformanceCoordinator();
    ~PerformanceCoordinator();

    void initialise(const juce::String& dbPath = "");
    void shutdown();

    // --- Consumer access ---
    StateAPI& state();
    EngineAPI& engine();

    // Lua execution callback (set by main after LuaEngine is created)
    std::function<std::string(const std::string& code)> luaExecutor;

    // --- Song lifecycle ---
    std::string createSong(const juce::String& name);
    void loadSong(const std::string& songId);
    bool restoreSession();
    void unloadSong();

    // --- Persistence ---
    void save();  // flush state to SQLite
    void captureProcessorState();  // grab all plugin binary blobs into state

    // --- Undo/Redo ---
    void onUndoRedoRestore();  // post-restore fixup after undo/redo

    // --- Song state snapshots ---
    void saveInitialState();
    void loadInitialState();

    // --- Score ---
    // Score steps are song bindings with isScoreStep=true.
    // Replay executes score-step actions in order from initial state.
    void replayScore(int upToStep = -1);

    // --- Track presets (cross-cutting: state + engine) ---
    void saveTrackPreset(const juce::String& trackId, const juce::String& presetName);
    void loadTrackPreset(const juce::String& trackId, const juce::String& presetName);
    std::vector<juce::String> listTrackPresets();

    // --- Automation ---
    using AutomationCallback = std::function<void(float)>;
    using EasingFn = std::function<float(float)>;
    int interpolate(float from, float to, float durationSec,
                    AutomationCallback callback, EasingFn easing = nullptr);
    int delay(float delaySec, std::function<void()> callback);
    void cancelAutomation(int handle);
    void cancelAllAutomation();

    // --- Action dispatch (for MIDI bindings) ---
    void executeAction(const std::string& actionName, const juce::var& args, float value);

    // --- MIDI devices ---
    void refreshMidiDevices();
    void startMidiLearn(const std::string& deviceId,
                        std::function<void(const std::string& controlType, int channel, int number)> callback);
    void cancelMidiLearn();
    void setMidiDeviceMonitor(const std::string& deviceId,
                              std::function<void(const std::string& description,
                                                 const std::string& type, int channel, int number)> callback);
    void clearMidiDeviceMonitor();
    int64_t getMidiDeviceActivityMs(const std::string& deviceId);
    int64_t getMidiPortActivityMs(const std::string& portName);

    // Global MIDI monitor (for debug pane — fires for all devices)
    void setGlobalMidiMonitor(std::function<void(const std::string& deviceName,
                                                  const std::string& description,
                                                  const std::string& type, int channel,
                                                  int number, int value)> callback);
    void clearGlobalMidiMonitor();

    // --- Sequencer (optional — null means disabled) ---
    SequencerAPI* sequencer();  // may return nullptr
    Arrangement& arrangement() { return arrangementImpl; }

    // --- Recording ---
    void startRecordMode();   // enter record mode and start playback
    void stopRecordMode();    // exit record mode (keeps playing)
    void reloadAudioFiles();  // re-scan regions and load audio files into engine
    bool isInRecordMode() const { return recordModeActive; }

    // --- Logging ---
    void log(const juce::String& message);

private:
    std::unique_ptr<StateAPI> stateAPI;
    std::unique_ptr<EngineAPI> engineAPI;
    std::unique_ptr<PersistenceLayer> persistence;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<EngineSync> engineSync;
    std::unique_ptr<AutomationEngine> automationEngine;
    std::unique_ptr<MIDIEngine> midiEngine;
    std::unique_ptr<SongRuntime> songRuntime;
    std::unique_ptr<SequencerAPI> sequencerImpl;
    Arrangement arrangementImpl;
    double lastSequencerTimeMs = 0.0;
    double lastSequencerBeat = 0.0;
    double lastActionScanBeat = 0.0;

    // Recording state
    bool recordModeActive = false;  // user explicitly requested recording
    bool isRecording = false;
    std::map<std::pair<int,int>, double> openNotes;  // {noteNumber, channel} → beatOffset
    std::vector<std::string> recordingTrackIds;         // MIDI tracks being recorded into
    struct AudioRecordSession {
        std::string trackId;
        std::string regionId;
        std::unique_ptr<AudioRecordFIFO> fifo;
        std::unique_ptr<AudioWriterThread> writer;
    };
    std::vector<AudioRecordSession> audioRecordSessions;
    double recordStartBeat = 0.0;
    void startRecording();
    void stopRecording();
    void drainRecordFIFO();
    void computeAudioPeaks(TakeState& take);
    void loadAudioFilesIntoEngine();
    void syncTempoFromState();

    void timerCallback() override;
    void populatePluginCatalog();
    void registerBuiltinActions();
    void restoreBindings();
    void onStateEvent(const StateEvent& event);
    void ensureDefaultPreset(const std::string& parentId, const std::string& effectId,
                             const std::string& pluginId, PresetKind kind);
    int stateSubscriptionId = -1;
};
