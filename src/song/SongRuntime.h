#pragma once
#include "song/Song.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <unordered_map>

class AudioEngine;

// Manages the live state of a loaded song
class SongRuntime {
public:
    SongRuntime(AudioEngine& engine);

    // Load a song definition — instantiates plugins, builds chains, activates bindings
    void load(const SongDef& song);

    // Unload current song
    void unload();

    // Called by MIDIEngine for control events
    void handleControl(int channel, int ccNumber, int value);
    void handleNoteOn(int channel, int noteNumber, int velocity);
    void handleNoteOff(int channel, int noteNumber);
    void handlePitchBend(int channel, int value);
    void handlePressure(int channel, int value);

    // Access a loaded plugin's parameter by instrument/effect name and param name
    // Returns nullptr if not found
    juce::AudioProcessorParameter* findParam(const juce::String& instrumentName,
                                              const juce::String& effectName,
                                              const juce::String& paramName);

    bool isLoaded() const { return loaded; }
    const juce::String& getSongName() const { return songName; }

private:
    AudioEngine& engine;
    juce::String songName;
    bool loaded = false;

    // Bindings indexed by control type for fast dispatch
    std::unordered_map<MIDIControl, std::vector<ControlHandler>, MIDIControlHash> controlMap;

    void dispatchControl(const MIDIControl& control, float value);
};
