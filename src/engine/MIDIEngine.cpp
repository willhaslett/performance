#include "engine/MIDIEngine.h"

MIDIEngine::MIDIEngine(juce::AudioDeviceManager& dm)
    : deviceManager(dm) {}

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
    auto sourceName = source ? source->getName() : "unknown";

    if (message.isNoteOn()) {
        DBG("[MIDI] " + sourceName + " | Note ON: " +
            juce::MidiMessage::getMidiNoteName(message.getNoteNumber(), true, true, 3) +
            " vel=" + juce::String(message.getVelocity()));
    } else if (message.isNoteOff()) {
        DBG("[MIDI] " + sourceName + " | Note OFF: " +
            juce::MidiMessage::getMidiNoteName(message.getNoteNumber(), true, true, 3));
    } else if (message.isController()) {
        DBG("[MIDI] " + sourceName + " | CC " +
            juce::String(message.getControllerNumber()) +
            " = " + juce::String(message.getControllerValue()));
    } else {
        DBG("[MIDI] " + sourceName + " | " + message.getDescription());
    }
}
