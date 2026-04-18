#pragma once
#include <map>
#include <string>
#include <vector>
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

struct ActionInfo {
    std::string id;
    std::string name;
    std::string label;
    std::string paramSchema;  // JSON
    std::string luaCode;      // custom action body (empty for built-in)
    std::string songId;       // empty = global, non-empty = song-scoped
    int durationParamIndex = -1;  // which arg index is the duration (-1 = none)
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
    std::string id;
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
    std::string id;
    double beat = 0.0;         // absolute beat position
    std::string actionId;      // references ActionInfo
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
struct MidiEventState {
    double beatOffset = 0.0;
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

// A region is a time-positioned container of takes on a track.
struct RegionState {
    RegionId id;
    std::string type = "midi";  // "midi" or "audio"
    std::string name;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    TakeId activeTakeId;   // which take plays back
    bool muted = false;         // region-level mute (skipped during playback)
    bool looped = false;       // region loops until next region or loopEndBeat
    double loopEndBeat = 0.0;  // 0 = auto (extend to next region), >0 = explicit end
    double quantize = 0.0;     // non-destructive quantize grid in beats (0 = off)
    std::vector<TakeState> takes;

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
    bool midiEnabled = true;   // receives MIDI notes (instrument tracks)
    bool audioEnabled = true;  // passes audio signal (all tracks)
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
    std::vector<RegionState> regions;
    std::vector<ActionEventData> actionData;  // only for Action tracks
};

struct BusState {
    BusId id;
    std::string name;
    float outputGain = 1.0f;
    bool audioEnabled = true;
    int position = 0;
    std::string outputTarget;  // "" = master, "none" = disconnected, UUID = another bus
    std::vector<EffectState> effects;
};

struct BindingState {
    std::string id;
    std::string songId;    // empty = global binding
    std::string deviceId;  // empty = any device
    std::string controlType;
    int channel = 0;
    int number = 0;
    std::string actionId;
    std::string args;  // JSON
    std::string description;
    bool isScoreStep = false;
    int scorePosition = -1;  // order within score (-1 = not a score step)
};

// Tempo change at a specific beat position.
// The first event (beat 0) sets the initial tempo.
struct TempoEvent {
    double beat = 0.0;
    double bpm = 120.0;
};

// Time signature change at a specific beat position.
// The first event (beat 0) sets the initial time signature.
struct TimeSignatureEvent {
    double beat = 0.0;
    int numerator = 4;
    int denominator = 4;
};

struct SongState {
    SongId id;
    std::string name;
    float masterGain = 1.0f;
    bool masterAudioEnabled = true;
    std::vector<TrackState> tracks;
    std::vector<BusState> busses;
    std::vector<EffectState> masterEffects;
    std::vector<BindingState> bindings;      // song-scoped bindings (includes score steps)
    std::vector<std::string> deviceIds;    // which devices this song uses
    std::string initialState;                 // JSON snapshot

    // Tempo and time signature maps (sorted by beat)
    std::vector<TempoEvent> tempoEvents;           // empty = use sequencer default (120)
    std::vector<TimeSignatureEvent> timeSigEvents;  // empty = use sequencer default (4/4)

    // Cycle playback (per-song)
    double cycleStart = 0.0;
    double cycleEnd = 0.0;      // 0 = no cycle set
    bool cycleEnabled = false;

    // Action events — beat-triggered actions on the timeline
    struct ActionEvent {
        std::string id;
        double beat = 0.0;
        std::string actionId;    // references ActionInfo
        std::string argsJson;    // JSON array of arguments
    };
    std::vector<ActionEvent> actionEvents;

    // Selection state (observable, not persisted)
    std::vector<TrackId> selectedTrackIds;
    std::vector<BusId> selectedBusIds;
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
};
