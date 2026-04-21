#pragma once
#include <juce_core/juce_core.h>
#include "state/ActionAlgebra.h"
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

    // Action-scoped Lua callback — sets `args` (1-based Lua table) and
    // `value` (number) as globals before running the code. Used by the
    // algebra interpreter for Op::Lua nodes.
    std::function<void(const std::string& code,
                       const std::vector<ActionAlgebra::Value>& args,
                       float midiValue)> luaActionExecutor;

    // Fired once per song-load completion. Used by the GUI for side effects
    // that shouldn't live in the coordinator (e.g. the stale-ref repair dialog).
    std::function<void()> onSongLoaded;

    // --- Song lifecycle ---
    std::string createSong(const juce::String& name);
    std::string createDefaultSong(const std::string& name);  // DLS Electric Piano + Audio In template
    bool needsStartupSongChooser() const;
    void syncPluginCatalog();  // rebuild state plugin catalog from engine after a fresh scan
    void loadSong(const std::string& songId);
    bool restoreSession();
    void unloadSong();

    // --- Live looping (see docs/LIVE_LOOPING.md) ---
    // Toggle loop-record state on a track. Transitions:
    //   Off → Armed         (user taps record; waits for next cycle wrap)
    //   Armed → Off         (user cancels before wrap)
    //   Recording → StopPending  (user taps record again during capture)
    //   StopPending → Recording  (user cancels the stop before wrap)
    // Wrap-driven transitions (Armed → Recording, StopPending → Off)
    // happen in onCycleWrap, which is called from the coordinator's
    // timer when the audio-thread beat wraps backward.
    void toggleLoopRecord(const TrackId& trackId);
    // Escape hatch for stuck or intentional-stop scenarios. Armed →
    // Off with no capture. Recording/StopPending → Off; if at least
    // one full cycle was captured, finalize the take with that
    // length; otherwise drop the take (and its freshly-created loop
    // region, if we just added it). Always balances `endCapture`.
    void forceStopLoopRecord(const TrackId& trackId);
    std::string getLoopRecordState(const TrackId& trackId) const;

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

    // --- Offline render (bounce) ---
    // Renders [startBeat, endBeat) to a stereo WAV at the given tempo, faster
    // than realtime. The engine's device callback outputs silence during the
    // render. Synchronous — returns when rendering is done. Not for calling
    // from the audio thread. See src/rendering/OfflineRenderer.h for caveats.
    struct BounceResult {
        bool ok = false;
        juce::String errorMessage;
        double wallClockSeconds = 0.0;
        double audioDurationSeconds = 0.0;
        double startBeat = 0.0;
        double endBeat = 0.0;
    };
    BounceResult bounce(const juce::File& outputFile, double startBeat, double endBeat);
    // Overload: when no range is given, use the current cycle region if
    // cycle mode is enabled on the sequencer. Errors if cycle is off.
    BounceResult bounce(const juce::File& outputFile);

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
                        std::function<void(const std::string& controlType, int channel, int number,
                                           const std::string& portName)> callback);
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
    // Algebra interpreter for actions with a body. Set up in initialise();
    // adapters live in the .cpp (they hold references to the above engines).
    struct AlgebraAdapters;
    std::unique_ptr<AlgebraAdapters> algebraAdapters;
    std::unique_ptr<MIDIEngine> midiEngine;
    std::unique_ptr<SongRuntime> songRuntime;
    std::unique_ptr<SequencerAPI> sequencerImpl;
    Arrangement arrangementImpl;
    double lastSequencerTimeMs = 0.0;
    double lastSequencerBeat = 0.0;
    double lastActionScanBeat = 0.0;

    // Debounced autosave — tracks the time of the most recent state mutation.
    // timerCallback saves when (now - lastStateChangeMs) > 3 seconds AND dirty.
    double lastStateChangeMs = 0.0;
    int autosaveSubscriptionId = -1;
    bool startupChooserNeeded = false;

    // Recording state
    bool recordModeActive = false;      // user explicitly requested arrangement recording
    bool isRecording = false;           // any capture flow active (arrangement || looper)
    int captureRefCount = 0;            // refcount across flows; flips engine gate on 0↔1
    bool arrangementRecordingActive = false;  // true while the arrangement flow owns a session
    // Turn MIDI capture on/off via refcount. Each caller (arrangement
    // startRecording, looper punch-in) is responsible for one begin/end
    // pair. Engine gates are only flipped on 0↔1 transitions so flows
    // can coexist without trampling each other.
    void beginCapture(double originBeat);
    void endCapture();
    std::map<std::pair<int,int>, double> openNotes;  // {noteNumber, channel} → beatOffset
    std::vector<TrackId> recordingTrackIds;              // MIDI tracks being recorded into
    struct AudioRecordSession {
        TrackId trackId;
        RegionId regionId;
        std::unique_ptr<AudioRecordFIFO> fifo;
        std::unique_ptr<AudioWriterThread> writer;
    };
    std::vector<AudioRecordSession> audioRecordSessions;
    double recordStartBeat = 0.0;
    // Diagnostics — debugging the "no events captured" issue.
    int diagFifoPops = 0;
    int diagDroppedNegativeOffset = 0;
    double lastRecordDiagLogMs = 0.0;
    void startRecording();
    void stopRecording();
    void drainRecordFIFO();
    void computeAudioPeaks(TakeState& take);
    void loadAudioFilesIntoEngine();
    void syncTempoFromState();

    // Loop recording — runtime state per track, driven by the
    // cycle-wrap hook. See docs/LIVE_LOOPING.md.
    enum class LoopRecordState { Off, Armed, Recording, StopPending };
    struct LoopRecordEntry {
        LoopRecordState state = LoopRecordState::Off;
        double punchInBeat = 0.0;   // global beat at which current recording began
        int cyclesRecorded = 0;     // wraps observed while Recording
    };
    std::map<std::string /*TrackId*/, LoopRecordEntry> loopRecordStates;
    void onCycleWrap(double wrapBeat);   // called from timer when beat wraps

    void timerCallback() override;
    void populatePluginCatalog();
    void registerBuiltinActions();
    void restoreBindings();
    void onStateEvent(const StateEvent& event);
    void ensureDefaultPreset(const std::string& parentId, const std::string& effectId,
                             const PluginId& pluginId, PresetKind kind);
    int stateSubscriptionId = -1;
};
