#pragma once
#include "song/Song.h"
#include <unordered_map>
#include <vector>

// MIDI control dispatch — routes MIDI controls to bound action handlers.
class SongRuntime {
public:
    // Called by MIDIEngine for control events
    void handleControl(int channel, int ccNumber, int value);
    void handleNoteOn(int channel, int noteNumber, int velocity);
    void handleNoteOff(int channel, int noteNumber);
    void handlePitchBend(int channel, int value);
    void handlePressure(int channel, int value);

    // Binding management
    void addBinding(const MIDIControl& control, ControlHandler handler,
                    const juce::String& description = "");
    void removeBinding(const MIDIControl& control);
    void clearBindings();

    void unload();

private:
    std::unordered_map<MIDIControl, std::vector<ControlHandler>, MIDIControlHash> controlMap;

    void dispatchControl(const MIDIControl& control, float value);
};
