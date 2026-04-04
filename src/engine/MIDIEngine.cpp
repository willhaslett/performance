#include "engine/MIDIEngine.h"
#include "engine/AudioEngine.h"
#include "engine/Log.h"
#include "song/SongRuntime.h"

MIDIEngine::MIDIEngine(juce::AudioDeviceManager& dm, AudioEngine& ae)
    : deviceManager(dm), audioEngine(ae) {}

MIDIEngine::~MIDIEngine() {
    shutdown();
}

void MIDIEngine::initialise() {
    auto devices = juce::MidiInput::getAvailableDevices();

    perfLog("[MIDI] MIDI inputs:\n");
    for (auto& device : devices) {
        perfLog("[MIDI]   %s\n", device.name.toRawUTF8());
        deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
        deviceManager.addMidiInputDeviceCallback(device.identifier, this);
        enabledDevices.add(device.identifier);
    }

    if (devices.isEmpty())
        perfLog("[MIDI]   (none)\n");
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
        if (message.isNoteOn())
            songRuntime->handleNoteOn(ch, message.getNoteNumber(), message.getVelocity());
        else if (message.isNoteOff())
            songRuntime->handleNoteOff(ch, message.getNoteNumber());
        else if (message.isController())
            songRuntime->handleControl(ch, message.getControllerNumber(), message.getControllerValue());
        else if (message.isPitchWheel())
            songRuntime->handlePitchBend(ch, message.getPitchWheelValue());
        else if (message.isChannelPressure())
            songRuntime->handlePressure(ch, message.getChannelPressureValue());
    }

    // Monitor mode: log everything to stderr
    if (!monitorMode) return;

    auto ch = message.getChannel();

    if (message.isNoteOn()) {
        perfLog("[MIDI] ch=%d  NOTE ON  %s (%d)  vel=%d\n",
                ch, juce::MidiMessage::getMidiNoteName(message.getNoteNumber(), true, true, 3).toRawUTF8(),
                message.getNoteNumber(), message.getVelocity());
    } else if (message.isNoteOff()) {
        perfLog("[MIDI] ch=%d  NOTE OFF %s (%d)\n",
                ch, juce::MidiMessage::getMidiNoteName(message.getNoteNumber(), true, true, 3).toRawUTF8(),
                message.getNoteNumber());
    } else if (message.isController()) {
        perfLog("[MIDI] ch=%d  CC %d = %d\n",
                ch, message.getControllerNumber(), message.getControllerValue());
    } else if (message.isPitchWheel()) {
        perfLog("[MIDI] ch=%d  PITCH %d\n", ch, message.getPitchWheelValue());
    } else if (message.isAftertouch()) {
        perfLog("[MIDI] ch=%d  AFTERTOUCH note=%d val=%d\n",
                ch, message.getNoteNumber(), message.getAfterTouchValue());
    } else if (message.isChannelPressure()) {
        perfLog("[MIDI] ch=%d  PRESSURE %d\n", ch, message.getChannelPressureValue());
    } else if (message.isProgramChange()) {
        perfLog("[MIDI] ch=%d  PROGRAM %d\n", ch, message.getProgramChangeNumber());
    }
}
