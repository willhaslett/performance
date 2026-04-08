#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include <map>

class AudioEngine;
class StateAPI;
class SongRuntime;

class MIDIEngine : public juce::MidiInputCallback {
public:
    MIDIEngine(juce::AudioDeviceManager& deviceManager, AudioEngine& audioEngine,
               StateAPI& stateAPI);
    ~MIDIEngine() override;

    void initialise();
    void shutdown();

    void setSongRuntime(SongRuntime* runtime) { songRuntime = runtime; }
    void setMonitorMode(bool enabled) { monitorMode = enabled; }

    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

private:
    void refreshDeviceMapping();

    juce::AudioDeviceManager& deviceManager;
    AudioEngine& audioEngine;
    StateAPI& stateAPI;
    SongRuntime* songRuntime = nullptr;
    juce::StringArray enabledDevices;
    std::map<juce::String, std::string> portToDeviceId;  // cached at init
    bool monitorMode = false;
};
