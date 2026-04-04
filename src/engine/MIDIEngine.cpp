#include "engine/MIDIEngine.h"
#include "engine/AudioEngine.h"

MIDIEngine::MIDIEngine(juce::AudioDeviceManager& dm, AudioEngine& ae)
    : deviceManager(dm), audioEngine(ae) {}

MIDIEngine::~MIDIEngine() {
    shutdown();
}

void MIDIEngine::initialise() {
    auto devices = juce::MidiInput::getAvailableDevices();

    DBG("Available MIDI inputs:");
    for (auto& device : devices) {
        DBG("  " + device.name + " (" + device.identifier + ")");
        deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
        deviceManager.addMidiInputDeviceCallback(device.identifier, this);
        enabledDevices.add(device.identifier);
    }

    if (devices.isEmpty())
        DBG("No MIDI input devices found");
}

void MIDIEngine::shutdown() {
    for (auto& id : enabledDevices)
        deviceManager.removeMidiInputDeviceCallback(id, this);
    enabledDevices.clear();
}

void MIDIEngine::handleIncomingMidiMessage(juce::MidiInput* source,
                                            const juce::MidiMessage& message) {
    // Forward all MIDI to the audio engine's graph
    audioEngine.injectMidi(message);

    // Log control messages (notes are too noisy during playing)
    if (message.isController()) {
        auto sourceName = source ? source->getName() : "unknown";
        DBG("[MIDI] " + sourceName + " | CC " +
            juce::String(message.getControllerNumber()) +
            " = " + juce::String(message.getControllerValue()));
    }
}
