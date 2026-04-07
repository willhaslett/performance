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

struct PresetInfo {
    std::string id;
    std::string pluginId;
    std::string name;
    std::string statePath;
};

struct ActionInfo {
    std::string id;
    std::string name;
    std::string label;
    std::string paramSchema;  // JSON
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
    std::string controlType;
    int channel = 0;
    int number = 0;
    std::string actionId;
    std::string args;  // JSON
    std::string description;
};

struct SongState {
    std::string id;
    std::string name;
    float masterGain = 1.0f;
    std::vector<TrackState> tracks;
    std::vector<BusState> busses;
    std::vector<EffectState> masterEffects;
    std::vector<BindingState> bindings;
    std::string initialState;  // JSON snapshot
    std::string score;          // JSON action list

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
    std::map<std::string, std::string> config;
};
