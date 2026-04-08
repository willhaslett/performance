#pragma once
#include <map>
#include <string>
#include <vector>

// In-memory state model — pure C++ structs, no JUCE dependency.
// This is the live SSOT while the app is running.

// --- Catalog (immutable templates, reusable across songs) ---

struct PluginInfo {
    std::string id;
    std::string name;
    std::string manufacturer;
    std::string formatId;  // AU identifier string
    bool isInstrument = false;
};

enum class PresetKind { Instrument, Effect, Track };

struct PresetInfo {
    std::string id;
    std::string pluginId;
    std::string name;
    std::string statePath;
    PresetKind kind = PresetKind::Instrument;
};

struct ActionInfo {
    std::string id;
    std::string name;
    std::string label;
    std::string paramSchema;  // JSON
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

struct EffectState {
    std::string id;
    std::string name;
    std::string pluginId;
    std::string presetId;
    int position = 0;
    LoadStatus loadStatus = LoadStatus::None;
    std::string processorState;      // base64 blob from getStateInformation
    std::string processorStateHash;  // sha256 of raw blob (for dirty detection)
};

struct SendState {
    std::string id;
    std::string busId;
    float gain = 1.0f;
};

struct TrackState {
    std::string id;
    std::string name;
    std::string pluginId;
    std::string presetId;
    float outputGain = 1.0f;
    bool midiEnabled = true;
    int position = 0;
    LoadStatus instrumentLoadStatus = LoadStatus::None;
    std::string processorState;      // base64 blob from getStateInformation
    std::string processorStateHash;  // sha256 of raw blob (for dirty detection)
    std::vector<EffectState> effects;
    std::vector<SendState> sends;
};

struct BusState {
    std::string id;
    std::string name;
    float outputGain = 1.0f;
    int position = 0;
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

struct SongState {
    std::string id;
    std::string name;
    float masterGain = 1.0f;
    std::vector<TrackState> tracks;
    std::vector<BusState> busses;
    std::vector<EffectState> masterEffects;
    std::vector<BindingState> bindings;      // song-scoped bindings (includes score steps)
    std::vector<std::string> deviceIds;    // which devices this song uses
    std::string initialState;                 // JSON snapshot

    // Selection state (observable, not persisted)
    std::vector<std::string> selectedTrackIds;
    std::vector<std::string> selectedBusIds;
};

// Top-level application state
struct AppState {
    std::string currentSongId;
    std::vector<SongState> songs;
    std::vector<PluginInfo> plugins;
    std::vector<PresetInfo> presets;
    std::vector<ActionInfo> actions;
    std::vector<DeviceState> devices;           // catalog of registered controllers
    std::vector<BindingState> globalBindings;  // song_id empty = applies everywhere
    std::map<std::string, std::string> config;
};
