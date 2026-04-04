#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>
#include <map>
#include <unordered_map>

// Identifies a MIDI control event
struct MIDIControl {
    enum Type { CC, Note, PitchBend, Pressure };
    Type type;
    int channel = 0;  // 0 = any channel
    int number = 0;   // CC number or note number

    bool operator==(const MIDIControl& other) const {
        return type == other.type && channel == other.channel && number == other.number;
    }

    juce::String toString() const {
        juce::String s;
        switch (type) {
            case CC:        s = "CC " + juce::String(number); break;
            case Note:      s = "Note " + juce::String(number); break;
            case PitchBend: s = "PitchBend"; break;
            case Pressure:  s = "Pressure"; break;
        }
        if (channel > 0) s += " ch" + juce::String(channel);
        return s;
    }
};

struct MIDIControlHash {
    size_t operator()(const MIDIControl& c) const {
        return std::hash<int>()(c.type) ^ (std::hash<int>()(c.channel) << 4) ^ (std::hash<int>()(c.number) << 8);
    }
};

// Handler called when a control event fires. Value is 0.0-1.0 normalized.
using ControlHandler = std::function<void(float value)>;

// A binding from a MIDI control to a handler
struct ControlBinding {
    MIDIControl control;
    ControlHandler handler;
    juce::String description;
};

// An audio effect in a signal chain (AU plugin)
struct EffectDef {
    juce::String name;        // user-assigned name (e.g., "Reverb")
    juce::String pluginName;  // AU plugin name to search for
};

// A send from a track to a bus
struct SendDef {
    juce::String busName;
    float gain = 1.0f;        // linear gain, 0.0 = silent, 1.0 = unity
};

// MIDI effect placeholder (not wired yet)
struct MidiEffectDef {
    juce::String name;
    juce::String type;
    std::map<juce::String, float> params;
};

// A track: one instrument + optional effects + sends
struct TrackDef {
    juce::String name;
    juce::String pluginName;
    std::vector<MidiEffectDef> midiEffects;
    std::vector<EffectDef> effects;
    std::vector<SendDef> sends;
    float outputGain = 1.0f;
    bool midiEnabled = true;
};

// A bus: receives audio from track sends, processes through effects
struct BusDef {
    juce::String name;
    std::vector<EffectDef> effects;
    float outputGain = 1.0f;

    EffectDef& addEffect(const juce::String& effectName, const juce::String& pluginName) {
        effects.push_back({ effectName, pluginName });
        return effects.back();
    }
};

// A song: setup (tracks + busses) + mappings (control bindings)
struct SongDef {
    juce::String name;
    std::vector<TrackDef> tracks;
    std::vector<BusDef> busses;
    std::vector<ControlBinding> bindings;

    TrackDef& addTrack(const juce::String& name, const juce::String& pluginName) {
        tracks.push_back({ name, pluginName, {}, {}, {}, 1.0f, true });
        return tracks.back();
    }

    BusDef& addBus(const juce::String& name) {
        busses.push_back({ name, {}, 1.0f });
        return busses.back();
    }

    void bind(MIDIControl control, ControlHandler handler, const juce::String& description = "") {
        bindings.push_back({ control, std::move(handler), description });
    }
};
