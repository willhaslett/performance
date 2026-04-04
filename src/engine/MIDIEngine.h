#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>

class AudioEngine;

class MIDIEngine : public juce::MidiInputCallback {
public:
    MIDIEngine(juce::AudioDeviceManager& deviceManager, AudioEngine& audioEngine);
    ~MIDIEngine() override;

    void initialise();
    void shutdown();

    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

private:
    juce::AudioDeviceManager& deviceManager;
    AudioEngine& audioEngine;
    juce::StringArray enabledDevices;
};
