#include "engine/MIDIEngine.h"
#include "engine/AudioEngine.h"
#include "song/SongRuntime.h"

MIDIEngine::MIDIEngine(juce::AudioDeviceManager& dm, AudioEngine& ae)
    : deviceManager(dm), audioEngine(ae) {}

MIDIEngine::~MIDIEngine() {
    shutdown();
}

void MIDIEngine::initialise() {
    auto devices = juce::MidiInput::getAvailableDevices();

    DBG("MIDI inputs:");
    for (auto& device : devices) {
        DBG("  " + device.name);
        deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
        deviceManager.addMidiInputDeviceCallback(device.identifier, this);
        enabledDevices.add(device.identifier);
    }

    if (devices.isEmpty())
        DBG("  (none)");
}

void MIDIEngine::shutdown() {
    for (auto& id : enabledDevices)
        deviceManager.removeMidiInputDeviceCallback(id, this);
    enabledDevices.clear();
}

void MIDIEngine::handleIncomingMidiMessage(juce::MidiInput* source,
                                            const juce::MidiMessage& message) {
    // Always forward to audio graph (for note playback)
    audioEngine.injectMidi(message);

    // Dispatch control events to song runtime
    if (songRuntime) {
        auto ch = message.getChannel();
        if (message.isController())
            songRuntime->handleControl(ch, message.getControllerNumber(), message.getControllerValue());
        else if (message.isPitchWheel())
            songRuntime->handlePitchBend(ch, message.getPitchWheelValue());
        else if (message.isChannelPressure())
            songRuntime->handlePressure(ch, message.getChannelPressureValue());
    }

    // Monitor mode: log everything
    if (!monitorMode) return;

    auto ch = message.getChannel();

    if (message.isNoteOn()) {
        DBG("[MIDI] ch=" + juce::String(ch)
            + "  NOTE ON  " + juce::MidiMessage::getMidiNoteName(message.getNoteNumber(), true, true, 3)
            + " (" + juce::String(message.getNoteNumber()) + ")"
            + "  vel=" + juce::String(message.getVelocity()));
    } else if (message.isNoteOff()) {
        DBG("[MIDI] ch=" + juce::String(ch)
            + "  NOTE OFF " + juce::MidiMessage::getMidiNoteName(message.getNoteNumber(), true, true, 3)
            + " (" + juce::String(message.getNoteNumber()) + ")");
    } else if (message.isController()) {
        DBG("[MIDI] ch=" + juce::String(ch)
            + "  CC " + juce::String(message.getControllerNumber())
            + " = " + juce::String(message.getControllerValue()));
    } else if (message.isPitchWheel()) {
        DBG("[MIDI] ch=" + juce::String(ch)
            + "  PITCH " + juce::String(message.getPitchWheelValue()));
    } else if (message.isAftertouch()) {
        DBG("[MIDI] ch=" + juce::String(ch)
            + "  AFTERTOUCH note=" + juce::String(message.getNoteNumber())
            + " val=" + juce::String(message.getAfterTouchValue()));
    } else if (message.isChannelPressure()) {
        DBG("[MIDI] ch=" + juce::String(ch)
            + "  PRESSURE " + juce::String(message.getChannelPressureValue()));
    } else if (message.isProgramChange()) {
        DBG("[MIDI] ch=" + juce::String(ch)
            + "  PROGRAM " + juce::String(message.getProgramChangeNumber()));
    }
}
