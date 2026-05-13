#pragma once
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include "state/ActionAlgebra.h"
#include "state/Id.h"

// In-memory state model — pure C++ structs, no JUCE dependency.
// This is the live SSOT while the app is running.

// --- Catalog (immutable templates, reusable across songs) ---

struct PluginInfo {
    PluginId id;
    std::string name;
    std::string manufacturer;
    std::string formatId;  // AU identifier string
    bool isInstrument = false;
};

enum class PresetKind { Instrument, Effect, Track };

struct PresetInfo {
    PresetId id;
    PluginId pluginId;
    std::string name;
    std::string statePath;
    PresetKind kind = PresetKind::Instrument;
};

// ParamType — primitive kinds an action argument can be.
// See docs/ACTION_INSTANCES_REFACTOR.md for the grammar rationale.
enum class ParamType {
    ChannelRef,  // UUID of a track / bus / "Main" (master)
    PresetRef,   // UUID of a preset (app-wide; picker groups by plugin)
    Enum,        // one of enumValues
    Float,       // number with optional minValue/maxValue/default
    Morph,       // compound sub-action blob (escape hatch; edited by MorphEditor)
};

struct ParamSchema {
    std::string name;
    ParamType   type = ParamType::Float;
    bool        required = true;
    std::string defaultValue;  // type-interpreted as string; form parses

    // ChannelRef filters (empty means "any").
    // `scope` restricts to "track" / "bus" / "master".
    // `sourceTypes` further restricts track scope to source type names
    //   ("Instrument" / "AudioInput" / "Action").
    std::vector<std::string> scope;
    std::vector<std::string> sourceTypes;

    // Enum candidate values.
    std::vector<std::string> enumValues;

    // Float bounds (std::nullopt = unbounded).
    std::optional<double> minValue;
    std::optional<double> maxValue;
};

struct ActionInfo {
    ActionId id;
    std::string name;
    std::string label;
    std::vector<ParamSchema> params;   // typed schema — the source of truth
    std::string luaCode;               // legacy Lua action body (migrating to body's Op::Lua)
    SongId songId;                     // empty = global, non-empty = song-scoped
    int durationParamIndex = -1;       // which arg index is the duration (-1 = none)
    // Algebra tree body; see docs/ACTION_ALGEBRA.md. Empty op=Set with no
    // target + no `to` is treated as "unset" and the caller falls back to
    // the legacy C++ dispatch in executeAction. Built-ins migrate to bodies
    // one at a time; customs land here in step 7.
    // Note: body is in-memory only today; persistence lands in step 8.
    ActionAlgebra::ActionNode body;
    bool hasBody = false;

    // Optional expander for dynamic trees — used by built-ins whose body
    // shape depends on runtime state (e.g., morphToPreset expands to a
    // Parallel of Interpolates over the plugin's live parameter count).
    // If set, the interpreter calls the expander with the positional arg
    // values at trigger time and runs the returned concrete tree. Static
    // bodies (above) are only used when no expander is present.
    // Not serialized — expanders are registered in code, not data.
    std::function<ActionAlgebra::ActionNode(const std::vector<ActionAlgebra::Value>&)> expander;
};

// --- Devices (registered physical controllers) ---

struct ControlDef {
    std::string name;         // "Fader 1", "Pad 3", "Sustain Pedal"
    std::string controlType;  // "cc", "note", "pitchbend", "pressure"
    int channel = 0;
    int number = 0;
    std::string group;        // "Faders", "Pads", "Transport" (for UI grouping)
};

struct DeviceState {
    DeviceId id;
    std::string name;           // "KeyLab 88 MkII"
    std::string midiPortName;   // JUCE MidiInput identifier for port matching
    std::vector<ControlDef> controls;
};

// --- Session state (per-song, mutable at runtime) ---

enum class LoadStatus { None, Pending, Loaded, Failed };
enum class TrackSourceType { Instrument, AudioInput, Action };
enum class ChannelMode { Mono, Stereo };

// Action event data — stored directly on Action tracks (no regions)
struct ActionEventData {
    ActionEventId id;
    double beat = 0.0;         // absolute beat position
    ActionId actionId;         // references ActionInfo
    std::string argsJson = "[]";
};

struct EffectState {
    EffectId id;
    std::string name;
    PluginId pluginId;
    PresetId presetId;
    int position = 0;
    LoadStatus loadStatus = LoadStatus::None;
    std::string processorState;      // base64 blob from getStateInformation
    std::string processorStateHash;  // sha256 of raw blob (for dirty detection)
};

struct SendState {
    SendId id;
    BusId busId;
    float gain = 1.0f;
};

// A single MIDI event (noteOn, noteOff, CC, aftertouch, etc.)
//
// CONVENTION — read carefully before doing arithmetic on `beatOffset`.
// This field is REGION-LOCAL: it's the beat offset from the parent
// region's `startBeat`. Storing region-local lets regions move,
// duplicate, and split cleanly because their events come along.
//
// THE HAZARD: any operation that aligns events to a global musical
// grid (quantize, snap to bar, scoring) must convert to GLOBAL beats
// first, otherwise the grid ends up offset by `region.startBeat % grid`
// and notes land off the timeline gridlines / out of sync with the
// metronome. This bit us on 2026-04-26 (commit ef486b4 fixed
// quantize). Anywhere you compute `round(beatOffset / something)`,
// `beatOffset % something`, or anything else that assumes a fixed
// origin: convert to `region.startBeat + beatOffset` first, do the
// math, then convert back if you need a region-local result.
struct MidiEventState {
    double beatOffset = 0.0;   // REGION-LOCAL beat offset; see comment above
    int status = 0x90;
    int channel = 1;
    int data1 = 60;
    int data2 = 100;

    bool isNoteOn() const  { return (status & 0xF0) == 0x90 && data2 > 0; }
    bool isNoteOff() const { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0); }
};

// A take within a region. MIDI takes have events, audio takes have a file path.
struct TakeState {
    TakeId id;
    std::string name;
    std::vector<MidiEventState> events;   // MIDI takes
    std::string filePath;                  // audio takes — WAV path
    double recordTempo = 120.0;            // tempo at recording time (for beat↔sample)
    int sampleRate = 48000;                // sample rate of recorded audio
    int channelCount = 2;                  // channels in the audio file

    // Waveform peak cache (computed, not persisted)
    struct PeakData {
        int samplesPerPeak = 256;
        std::vector<std::pair<float, float>> peaks;  // {min, max} per chunk
    };
    PeakData peakData;
};

// Looper-only: queued or in-flight action on a track's loop region.
// Boss-RC-style: bootstrap (no cycle yet) is tap-to-start / tap-to-stop,
// directly transitioning None ↔ Capturing*. Once a master cycle exists,
// taps queue to the next wrap — None → Queued → (at wrap) Capturing →
// (at next wrap) commit → None. The queued window keeps existing content
// playing and lets the user warm up; the capture window has the existing
// content silenced and the new input recorded.
//   None             - track is just playing (or silent)
//   ReplaceQueued    - tapped during established cycle; at next wrap
//                      becomes CapturingReplace
//   OverdubQueued    - tapped during established cycle; at next wrap
//                      becomes CapturingOverdub
//   CapturingReplace - recording right now; on commit, captured events
//                      replace existing content
//   CapturingOverdub - recording right now; on commit, captured events
//                      are merged into existing content
enum class LoopAction {
    None,
    ReplaceQueued,
    OverdubQueued,
    CapturingReplace,
    CapturingOverdub,
};

// A region is a time-positioned container of takes on a track.
struct RegionState {
    RegionId id;
    std::string type = "midi";  // "midi" or "audio"
    std::string name;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    TakeId activeTakeId;   // which take plays back
    TakeId pendingTakeId;  // runtime only — set by a take-swap request, applied
                           // at the next cycle wrap in looper mode. Not persisted.
    bool muted = false;         // region-level mute (skipped during playback)
    bool looped = false;       // region loops until next region or loopEndBeat
    double loopEndBeat = 0.0;  // 0 = auto (extend to next region), >0 = explicit end
    double quantize = 0.0;     // non-destructive quantize grid in beats (0 = off)
    std::vector<TakeState> takes;

    // --- Looper-only runtime state. Not persisted. ---
    // Performer-facing capture state machine — None / CapturingReplace /
    // CapturingOverdub. Set by replaceLoop/overdubLoop; cleared by
    // commitLoopAction. Undo/redo isn't tracked here — it goes through
    // the app-level UndoHistory, which already snapshots cycle length,
    // events, and lengthBeats together (one undo step = one commit).
    LoopAction loopAction = LoopAction::None;

    // Convenience: get the active take
    TakeState* activeTake() {
        for (auto& t : takes)
            if (t.id == activeTakeId) return &t;
        return takes.empty() ? nullptr : &takes[0];
    }
    const TakeState* activeTake() const {
        for (auto& t : takes)
            if (t.id == activeTakeId) return &t;
        return takes.empty() ? nullptr : &takes[0];
    }
};

struct TrackState {
    TrackId id;
    std::string name;
    PluginId pluginId;
    PresetId presetId;
    uint32_t color = 0;        // track color (0 = use default for source type)
    float outputGain = 1.0f;
    bool armed = false;        // record-armed (records MIDI when playing)
    bool inputMonitoring = true; // pass live audio input to output (audio input tracks)
    bool muted = false;        // mute output (runtime, not persisted)
    bool soloed = false;       // solo this track (runtime, not persisted)
    int position = 0;
    LoadStatus instrumentLoadStatus = LoadStatus::None;
    std::string processorState;
    std::string processorStateHash;
    TrackSourceType sourceType = TrackSourceType::Instrument;
    ChannelMode channelMode = ChannelMode::Stereo;
    int inputChannelStart = -1;  // physical input channel index (-1 = none)
    int inputChannelCount = 0;   // 1 = mono, 2 = stereo
    std::string outputTarget;  // output routing: "" = master, "none" = disconnected, UUID = bus
    std::vector<EffectState> effects;
    std::vector<SendState> sends;
    std::vector<RegionState> regions;   // arrangement pool — used by Producer
    std::vector<RegionState> loops;     // looper pool — used by Looper. Invariant in
                                        // looper mode: each entry has startBeat == 0.
                                        // Completely independent of `regions`; Producer
                                        // never sees loops and Looper never sees regions.
    std::vector<ActionEventData> actionData;  // only for Action tracks
};

struct BusState {
    BusId id;
    std::string name;
    float outputGain = 1.0f;
    int position = 0;
    std::string outputTarget;  // "" = master, "none" = disconnected, UUID = another bus
    std::vector<EffectState> effects;
};

struct BindingState {
    BindingId id;
    SongId songId;         // empty = global binding
    DeviceId deviceId;     // empty = any device
    std::string controlType;
    int channel = 0;
    int number = 0;
    ActionId actionId;
    std::string args;  // JSON
    std::string description;
    bool isScoreStep = false;
    int scorePosition = -1;  // order within score (-1 = not a score step)
};

// Tempo / time-signature / key-signature events live on the project
// timeline alongside action events — the LLM and (eventually) the
// timeline UI treat them uniformly via the unified Event API. Storage
// stays per-type (separate vectors on SongState) for type safety;
// addressing across types uses the shared EventId space.
//
// Every project starts with a beat-0 tempo event (default 120) and a
// beat-0 timesig event (default 4/4) — invariant set by createSong.
// Key has no implicit default (empty vector = no key declared).
struct TempoEvent {
    EventId id;
    double beat = 0.0;
    double bpm = 120.0;
};

struct TimeSignatureEvent {
    EventId id;
    double beat = 0.0;
    int numerator = 4;
    int denominator = 4;
};

// Key as ABC-style strings: "C", "Am", "F#mix", "none". Empty vector =
// no key declared (writer emits K:none).
struct KeyEvent {
    EventId id;
    double beat = 0.0;
    std::string key = "C";
};

struct SongState {
    SongId id;
    std::string name;
    float masterGain = 1.0f;
    std::vector<TrackState> tracks;
    std::vector<BusState> busses;
    std::vector<EffectState> masterEffects;
    std::vector<BindingState> bindings;      // song-scoped bindings (includes score steps)
    std::vector<DeviceId> deviceIds;    // which devices this song uses
    std::string initialState;                 // JSON snapshot

    // Tempo and time signature maps (sorted by beat). createSong
    // populates each with a beat-0 default; subsequent edits append.
    std::vector<TempoEvent> tempoEvents;
    std::vector<TimeSignatureEvent> timeSigEvents;
    std::vector<KeyEvent> keyEvents;                // empty = no key declared (K:none in ABC)

    // Cycle playback (per-song). Used by both the Producer's cycle mode
    // and the Looper. When the app is in `AppMode::Looper`, the
    // invariants cycleStart == 0 and cycleEnabled == true are enforced
    // at the StateAPI layer, and cycleEnd is the project loop length.
    double cycleStart = 0.0;
    double cycleEnd = 0.0;      // 0 = no cycle set
    bool cycleEnabled = false;

    // Action events — beat-triggered actions on the timeline
    struct ActionEvent {
        ActionEventId id;
        double beat = 0.0;
        ActionId actionId;       // references ActionInfo
        std::string argsJson;    // JSON array of arguments
    };
    std::vector<ActionEvent> actionEvents;

    // Selection state (observable, not persisted) — plural, for grouped
    // UI actions (bulk fader, multi-track mute, etc.).
    std::vector<TrackId> selectedTrackIds;
    std::vector<BusId> selectedBusIds;

    // Focus — the singular "track I'm playing into right now." UI cursor
    // and primary target. Persisted per-song. Distinct from selection:
    // selection is a set for group actions, focus is one pointer for
    // routing and single-target actions. Empty = no focus.
    // See docs/LIVE_INPUT_AND_FOCUS.md.
    TrackId focusedTrackId;
};

// Workflow/session mode. Determines how the engine dispatches playback
// for the current song:
//   Arrangement - plays from track.regions (classic DAW). Cycle optional.
//   Looper      - plays from track.loops, cycle forced on at StateAPI layer.
// See docs/PANE_MODE_MODEL.md for the full design — in particular, why
// this lives on AppState (workflow/session) rather than SongState.
enum class AppMode {
    Arrangement,
    Looper,
};

// Top-level application state
struct AppState {
    SongId currentSongId;
    std::vector<SongState> songs;
    std::vector<PluginInfo> plugins;
    std::vector<PresetInfo> presets;
    std::vector<ActionInfo> actions;
    std::vector<DeviceState> devices;           // catalog of registered controllers
    std::vector<BindingState> globalBindings;  // song_id empty = applies everywhere
    std::map<std::string, std::string> config;
    AppMode currentMode = AppMode::Arrangement;
};
