#include "engine/MIDIEngine.h"
#include "engine/AudioEngine.h"
#include "engine/Log.h"
#include "api/StateAPI.h"
#include "song/SongRuntime.h"

MIDIEngine::MIDIEngine(juce::AudioDeviceManager& dm, AudioEngine& ae, StateAPI& state)
    : deviceManager(dm), audioEngine(ae), stateAPI(state) {}

MIDIEngine::~MIDIEngine() {
    shutdown();
}

void MIDIEngine::refreshDeviceMapping() {
    portToDeviceId.clear();
    for (auto& device : stateAPI.allDevices())
        portToDeviceId[juce::String(device.midiPortName)] = device.id;
}

void MIDIEngine::initialise() {
    refreshDeviceMapping();

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

void MIDIEngine::startLearn(const std::string& deviceId, LearnCallback callback) {
    learnDeviceId = deviceId;
    learnCallback = std::move(callback);
    perfLog("[MIDI] Learn mode started for device %s\n", deviceId.c_str());
}

void MIDIEngine::cancelLearn() {
    learnCallback = nullptr;
    learnDeviceId.clear();
    perfLog("[MIDI] Learn mode cancelled\n");
}

void MIDIEngine::handleIncomingMidiMessage(juce::MidiInput* source,
                                            const juce::MidiMessage& message) {
    // Always forward to audio graph (for note playback)
    audioEngine.injectMidi(message);

    // Resolve device ID from source port (cached, O(1))
    std::string deviceId;
    if (source) {
        auto it = portToDeviceId.find(source->getName());
        if (it != portToDeviceId.end())
            deviceId = it->second;
    }

    // MIDI Learn: intercept before normal dispatch (single-shot)
    if (learnCallback && (learnDeviceId.empty() || learnDeviceId == deviceId)) {
        std::string controlType;
        int ch = message.getChannel();
        int num = 0;

        if (message.isController()) {
            controlType = "cc"; num = message.getControllerNumber();
        } else if (message.isNoteOn()) {
            controlType = "note"; num = message.getNoteNumber();
        } else if (message.isPitchWheel()) {
            controlType = "pitchbend";
        } else if (message.isChannelPressure()) {
            controlType = "pressure";
        }

        if (!controlType.empty()) {
            auto cb = std::move(learnCallback);
            learnCallback = nullptr;
            learnDeviceId.clear();
            juce::MessageManager::callAsync([cb, controlType, ch, num] {
                cb(controlType, ch, num);
            });
            return;  // don't dispatch during learn
        }
    }

    // Dispatch control events to song runtime
    if (songRuntime) {
        auto ch = message.getChannel();
        if (message.isNoteOn())
            songRuntime->handleNoteOn(deviceId, ch, message.getNoteNumber(), message.getVelocity());
        else if (message.isNoteOff())
            songRuntime->handleNoteOff(deviceId, ch, message.getNoteNumber());
        else if (message.isController())
            songRuntime->handleControl(deviceId, ch, message.getControllerNumber(), message.getControllerValue());
        else if (message.isPitchWheel())
            songRuntime->handlePitchBend(deviceId, ch, message.getPitchWheelValue());
        else if (message.isChannelPressure())
            songRuntime->handlePressure(deviceId, ch, message.getChannelPressureValue());
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
