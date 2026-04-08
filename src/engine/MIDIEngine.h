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

    // MIDI Learn: capture next event from a specific device
    using LearnCallback = std::function<void(const std::string& controlType,
                                              int channel, int number)>;
    void startLearn(const std::string& deviceId, LearnCallback callback);
    void cancelLearn();

    // MIDI event monitoring: receive formatted events from a specific device
    using MonitorCallback = std::function<void(const std::string& description,
                                                const std::string& type, int channel, int number)>;
    void setDeviceMonitor(const std::string& deviceId, MonitorCallback callback);
    void clearDeviceMonitor();

    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;
    void refreshDeviceMapping();  // call after registering new devices

private:

    juce::AudioDeviceManager& deviceManager;
    AudioEngine& audioEngine;
    StateAPI& stateAPI;
    SongRuntime* songRuntime = nullptr;
    juce::StringArray enabledDevices;
    std::map<juce::String, std::string> portToDeviceId;  // cached at init
    bool monitorMode = false;
    LearnCallback learnCallback;
    std::string learnDeviceId;
    MonitorCallback deviceMonitorCallback;
    std::string monitorDeviceId;
};
