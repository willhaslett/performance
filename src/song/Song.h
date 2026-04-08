#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <string>
#include <unordered_map>

// Identifies a MIDI control event
struct MIDIControl {
    enum Type { CC, Note, PitchBend, Pressure };
    Type type;
    int channel = 0;       // 0 = any channel
    int number = 0;        // CC number or note number
    std::string deviceId;  // empty = any device

    bool operator==(const MIDIControl& other) const {
        return type == other.type && channel == other.channel
            && number == other.number && deviceId == other.deviceId;
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
        auto h = std::hash<int>()(c.type) ^ (std::hash<int>()(c.channel) << 4)
               ^ (std::hash<int>()(c.number) << 8);
        if (!c.deviceId.empty())
            h ^= std::hash<std::string>()(c.deviceId) << 16;
        return h;
    }
};

// Handler called when a control event fires. Value is 0.0-1.0 normalized.
using ControlHandler = std::function<void(float value)>;
