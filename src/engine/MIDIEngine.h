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
                                              int channel, int number,
                                              const std::string& portName)>;
    void startLearn(const std::string& deviceId, LearnCallback callback);
    void cancelLearn();

    // MIDI event monitoring: receive formatted events from a specific device
    using MonitorCallback = std::function<void(const std::string& description,
                                                const std::string& type, int channel, int number)>;
    void setDeviceMonitor(const std::string& deviceId, MonitorCallback callback);
    void clearDeviceMonitor();

    // Global monitor: fires for ALL devices (for debug pane)
    using GlobalMonitorCallback = std::function<void(const std::string& deviceName,
                                                      const std::string& description,
                                                      const std::string& type, int channel,
                                                      int number, int value)>;
    void setGlobalMonitor(GlobalMonitorCallback callback);
    void clearGlobalMonitor();

    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;
    void refreshDeviceMapping();  // call after registering new devices
    void rescanDevices();         // detect added/removed MIDI devices

    // Per-device activity tracking (for sidebar indicators)
    int64_t getDeviceLastActivityMs(const std::string& deviceId) const;
    int64_t getPortLastActivityMs(const juce::String& portName) const;

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
    GlobalMonitorCallback globalMonitorCallback;
    std::map<std::string, int64_t> deviceActivityMs;   // deviceId → last activity time
    std::map<juce::String, int64_t> portActivityMs;    // portName → last activity time
};
