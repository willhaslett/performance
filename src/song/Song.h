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
    juce::String name;            // user-assigned name (e.g., "Reverb")
    juce::String pluginName;      // AU plugin name to search for
    juce::String snapshotName;    // saved plugin state to restore (empty = default)

    juce::var toJson() const {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("name", name);
        obj->setProperty("plugin", pluginName);
        if (snapshotName.isNotEmpty())
            obj->setProperty("snapshot", snapshotName);
        return juce::var(obj);
    }

    static EffectDef fromJson(const juce::var& v) {
        EffectDef e;
        e.name = v.getProperty("name", "").toString();
        e.pluginName = v.getProperty("plugin", "").toString();
        e.snapshotName = v.getProperty("snapshot", "").toString();
        return e;
    }
};

// A send from a track to a bus
struct SendDef {
    juce::String busName;
    float gain = 1.0f;

    juce::var toJson() const {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("bus", busName);
        obj->setProperty("gain", gain);
        return juce::var(obj);
    }

    static SendDef fromJson(const juce::var& v) {
        SendDef s;
        s.busName = v.getProperty("bus", "").toString();
        s.gain = (float)v.getProperty("gain", 1.0);
        return s;
    }
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
    juce::String snapshotName;    // instrument snapshot
    std::vector<MidiEffectDef> midiEffects;
    std::vector<EffectDef> effects;
    std::vector<SendDef> sends;
    float outputGain = 1.0f;
    bool midiEnabled = true;

    juce::var toJson() const {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("name", name);
        obj->setProperty("plugin", pluginName);
        if (snapshotName.isNotEmpty())
            obj->setProperty("snapshot", snapshotName);

        juce::Array<juce::var> efx;
        for (auto& e : effects) efx.add(e.toJson());
        if (!efx.isEmpty()) obj->setProperty("effects", efx);

        juce::Array<juce::var> snd;
        for (auto& s : sends) snd.add(s.toJson());
        if (!snd.isEmpty()) obj->setProperty("sends", snd);

        if (outputGain != 1.0f) obj->setProperty("outputGain", outputGain);
        if (!midiEnabled) obj->setProperty("midiEnabled", false);

        return juce::var(obj);
    }

    static TrackDef fromJson(const juce::var& v) {
        TrackDef t;
        t.name = v.getProperty("name", "").toString();
        t.pluginName = v.getProperty("plugin", "").toString();
        t.snapshotName = v.getProperty("snapshot", "").toString();
        t.outputGain = (float)v.getProperty("outputGain", 1.0);
        t.midiEnabled = (bool)v.getProperty("midiEnabled", true);

        if (auto* efx = v.getProperty("effects", {}).getArray())
            for (auto& e : *efx) t.effects.push_back(EffectDef::fromJson(e));
        if (auto* snd = v.getProperty("sends", {}).getArray())
            for (auto& s : *snd) t.sends.push_back(SendDef::fromJson(s));

        return t;
    }
};

// A bus: receives audio from track sends, processes through effects
struct BusDef {
    juce::String name;
    std::vector<EffectDef> effects;
    float outputGain = 1.0f;

    EffectDef& addEffect(const juce::String& effectName, const juce::String& pluginName) {
        effects.push_back({ effectName, pluginName, {} });
        return effects.back();
    }

    juce::var toJson() const {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("name", name);

        juce::Array<juce::var> efx;
        for (auto& e : effects) efx.add(e.toJson());
        if (!efx.isEmpty()) obj->setProperty("effects", efx);

        if (outputGain != 1.0f) obj->setProperty("outputGain", outputGain);

        return juce::var(obj);
    }

    static BusDef fromJson(const juce::var& v) {
        BusDef b;
        b.name = v.getProperty("name", "").toString();
        b.outputGain = (float)v.getProperty("outputGain", 1.0);

        if (auto* efx = v.getProperty("effects", {}).getArray())
            for (auto& e : *efx) b.effects.push_back(EffectDef::fromJson(e));

        return b;
    }
};

// A song's structure: tracks + busses (serializable to JSON)
// Bindings (behavior) live separately in Lua
struct SongDef {
    juce::String name;
    std::vector<TrackDef> tracks;
    std::vector<BusDef> busses;
    std::vector<ControlBinding> bindings;  // runtime only, not serialized

    TrackDef& addTrack(const juce::String& trackName, const juce::String& pluginName) {
        tracks.push_back({ trackName, pluginName, {}, {}, {}, {}, 1.0f, true });
        return tracks.back();
    }

    BusDef& addBus(const juce::String& busName) {
        busses.push_back({ busName, {}, 1.0f });
        return busses.back();
    }

    void bind(MIDIControl control, ControlHandler handler, const juce::String& description = "") {
        bindings.push_back({ control, std::move(handler), description });
    }

    // --- JSON serialization ---

    juce::String toJson() const {
        auto* root = new juce::DynamicObject();
        root->setProperty("name", name);

        juce::Array<juce::var> trk;
        for (auto& t : tracks) trk.add(t.toJson());
        root->setProperty("tracks", trk);

        juce::Array<juce::var> bus;
        for (auto& b : busses) bus.add(b.toJson());
        root->setProperty("busses", bus);

        return juce::JSON::toString(juce::var(root));
    }

    static SongDef fromJson(const juce::String& jsonStr) {
        SongDef song;
        auto parsed = juce::JSON::parse(jsonStr);
        if (parsed.isVoid()) return song;

        song.name = parsed.getProperty("name", "").toString();

        if (auto* trk = parsed.getProperty("tracks", {}).getArray())
            for (auto& t : *trk) song.tracks.push_back(TrackDef::fromJson(t));
        if (auto* bus = parsed.getProperty("busses", {}).getArray())
            for (auto& b : *bus) song.busses.push_back(BusDef::fromJson(b));

        return song;
    }
};
