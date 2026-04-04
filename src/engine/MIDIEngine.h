#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

class MIDIEngine : public juce::MidiInputCallback {
public:
    MIDIEngine(juce::AudioDeviceManager& deviceManager);
    ~MIDIEngine() override;

    void initialise();
    void shutdown();

    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

private:
    juce::AudioDeviceManager& deviceManager;
    juce::StringArray enabledDevices;
};
