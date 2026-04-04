#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>
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

// Reference to a plugin parameter: instrument name + optional effect name + param index
struct ParamRef {
    juce::String instrumentName;
    juce::String effectName;  // empty = instrument itself
    int paramIndex = -1;
    juce::String paramName;   // for lookup by name
};

// An effect in a signal chain
struct EffectDef {
    juce::String name;        // user-assigned name (e.g., "Reverb")
    juce::String pluginName;  // AU plugin name to search for
};

// An instrument with its effects chain
struct InstrumentDef {
    juce::String name;        // user-assigned name (e.g., "Keys")
    juce::String pluginName;  // AU plugin name to search for
    std::vector<EffectDef> effects;
};

// Handler called when a control event fires. Value is 0.0-1.0 normalized.
using ControlHandler = std::function<void(float value)>;

// A binding from a MIDI control to a handler
struct ControlBinding {
    MIDIControl control;
    ControlHandler handler;
    juce::String description;  // for display (e.g., "CC 1 -> Reverb Mix")
};

// A song definition
struct SongDef {
    juce::String name;
    std::vector<InstrumentDef> instruments;
    std::vector<ControlBinding> bindings;

    InstrumentDef& addInstrument(const juce::String& name, const juce::String& pluginName) {
        instruments.push_back({ name, pluginName, {} });
        return instruments.back();
    }

    void bind(MIDIControl control, ControlHandler handler, const juce::String& description = "") {
        bindings.push_back({ control, std::move(handler), description });
    }
};
