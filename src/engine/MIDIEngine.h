#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>

class AudioEngine;
class SongRuntime;

class MIDIEngine : public juce::MidiInputCallback {
public:
    MIDIEngine(juce::AudioDeviceManager& deviceManager, AudioEngine& audioEngine);
    ~MIDIEngine() override;

    void initialise();
    void shutdown();

    void setSongRuntime(SongRuntime* runtime) { songRuntime = runtime; }
    void setMonitorMode(bool enabled) { monitorMode = enabled; }

    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

private:
    juce::AudioDeviceManager& deviceManager;
    AudioEngine& audioEngine;
    SongRuntime* songRuntime = nullptr;
    juce::StringArray enabledDevices;
    bool monitorMode = false;
};
