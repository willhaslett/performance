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
#include <optional>
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
    // Panic-button reset for the entire looper session. Stops
    // transport, drops any in-flight capture (gesture or bootstrap),
    // resets every per-track loopAction + undo/redo stack to a
    // clean slate, clears all loop event content, and resets
    // cycleEnd to 0 (back to bootstrap mode). Use when the session
    // is in some confused state and you want a guaranteed
    // known-good starting point.
    void resetLooperSession();

    // Phase 6 performer entry points. These wrap the StateAPI gestures
    // with bootstrap-mode awareness:
    //   cycleEnd == 0 (no cycle yet): tap-to-tap. First call starts
    //     immediate capture from beat 0 with transport rolling; second
    //     call sets cycleEnd from elapsed beats and commits.
    //   cycleEnd > 0 (established): normal queued behavior — gesture
    //     queues replace/overdub at next wrap.
    // Either gesture (replace or overdub) bootstraps; after first
    // bootstrap the actions diverge as designed.
    void replaceLoopGesture();
    void overdubLoopGesture();
    // Toggle transport play/stop. Mode-agnostic — works in Producer
    // and Looper both. Routes through the sequencer.
    void togglePlay();

    // Action-fire listener API. Any GUI that wants to know when an
    // action is dispatched (e.g. activity lights on the looper button
    // group) subscribes here. Every dispatch path — Lua, MIDI bindings,
    // GUI clicks — funnels through executeAction, which fans out to
    // every registered listener. Subscribe returns an id; unsubscribe
    // in the listener's destructor to avoid dangling captures.
    using ActionFireListener = std::function<void(const std::string& actionName)>;
    int  addActionFireListener(ActionFireListener listener);
    void removeActionFireListener(int id);

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
    // True if any recording flow is active — arrangement recording, an
    // in-flight looper bootstrap, or any focused-track loopAction other
    // than None. Used by the transport record button to drive its
    // toggle so a second press fires "stop" in any mode.
    bool isInRecordMode() const;

    // Read-only snapshot of an in-flight Looper gesture capture, for
    // the GUI to render notes as they're recorded. Returns nullptr when
    // no capture is open. Caller must consume on the message thread —
    // the underlying vector is mutated by drainRecordFIFO on that same
    // thread, so reads are safe but the pointer must not outlive the
    // call.
    struct InFlightLoopCapture {
        TrackId trackId;
        const std::vector<MidiEventState>* events;
    };
    std::optional<InFlightLoopCapture> getInFlightLoopCapture() const;

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
    // Wall-clock ms of the most recent captureProcessorState. Used to
    // skip a redundant capture on shutdown if autosave just did one.
    double lastCaptureMs = 0.0;
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

    // Action-fire fan-out (multi-listener; see addActionFireListener).
    std::map<int, ActionFireListener> actionFireListeners;
    int nextActionFireListenerId = 1;

    // Phase 6 looper — per-track gesture capture. Single capture in
    // flight at a time (target = focused track at wrap time). Events
    // drained from the FIFO during the capture cycle are appended here;
    // commitLoopAction reads them at the next wrap. See
    // docs/LIVE_INPUT_AND_FOCUS.md phase 6.
    struct LoopCaptureSlot {
        TrackId trackId;
        std::vector<MidiEventState> events;
    };
    std::optional<LoopCaptureSlot> activeLoopCapture;
    // Beat position at which the active capture started. Used at commit
    // time to compute elapsed beats — the very first commit also adopts
    // this elapsed value as the master cycle length.
    double captureStartBeat = 0.0;
    // Tap-to-toggle helper shared by replaceLoopGesture / overdubLoopGesture.
    void fireLoopCaptureToggle(LoopAction startKind, const char* label);
    // Stop edge of the toggle: commits the in-flight capture, computes
    // elapsed beats, and (on the very first commit) sets master cycle.
    void finishLoopCapture();

    // Per-mode playhead memory. The transport (InternalSequencer) is a
    // single shared clock; without this, playing in Looper for 5 minutes
    // and then switching back to Producer leaves the playhead at bar 143.
    // Stored in seconds (not beats) so a tempo change between leaving
    // and re-entering Arrangement preserves the wall-clock position the
    // user left, not a different musical position. Looper always re-
    // enters at beat 0 — its playhead is cycle-relative, so absolute
    // position outside the cycle is meaningless.
    AppMode lastSeenMode = AppMode::Arrangement;
    double stashedArrangementSeconds = 0.0;
    void handleModeChange();  // called from event subscription

    void timerCallback() override;
    void populatePluginCatalog();
    void registerBuiltinActions();
    void restoreBindings();
    void onStateEvent(const StateEvent& event);
    void ensureDefaultPreset(const std::string& parentId, const std::string& effectId,
                             const PluginId& pluginId, PresetKind kind);
    int stateSubscriptionId = -1;
};
