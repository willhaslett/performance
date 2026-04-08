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

void MIDIEngine::setDeviceMonitor(const std::string& deviceId, MonitorCallback callback) {
    monitorDeviceId = deviceId;
    deviceMonitorCallback = std::move(callback);
}

void MIDIEngine::clearDeviceMonitor() {
    deviceMonitorCallback = nullptr;
    monitorDeviceId.clear();
}

void MIDIEngine::setGlobalMonitor(GlobalMonitorCallback callback) {
    globalMonitorCallback = std::move(callback);
}

void MIDIEngine::clearGlobalMonitor() {
    globalMonitorCallback = nullptr;
}

void MIDIEngine::handleIncomingMidiMessage(juce::MidiInput* source,
                                            const juce::MidiMessage& message) {
    // Always forward to audio graph (for note playback)
    audioEngine.injectMidi(message);

    // Resolve device ID from source port (cached, O(1))
    std::string deviceId;
    juce::String sourcePortName;
    if (source) {
        sourcePortName = source->getName();
        auto it = portToDeviceId.find(sourcePortName);
        if (it != portToDeviceId.end())
            deviceId = it->second;
    }

    // MIDI Learn: intercept before normal dispatch (single-shot)
    // Match by device ID, or fall back to matching by port name for freshly registered devices
    bool learnMatch = false;
    if (learnCallback) {
        if (learnDeviceId.empty() || learnDeviceId == deviceId) {
            learnMatch = true;
        } else if (!learnDeviceId.empty() && sourcePortName.isNotEmpty()) {
            // Try matching by port name — the device may have been registered with this port name
            auto* device = stateAPI.findDevice(learnDeviceId);
            if (device && device->midiPortName == sourcePortName.toStdString())
                learnMatch = true;
        }
    }
    if (learnMatch) {
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

    // Device monitor callback: send formatted event descriptions to listener
    if (deviceMonitorCallback) {
        bool monitorMatch = (monitorDeviceId == deviceId);
        if (!monitorMatch && !monitorDeviceId.empty() && sourcePortName.isNotEmpty()) {
            auto* device = stateAPI.findDevice(monitorDeviceId);
            if (device && device->midiPortName == sourcePortName.toStdString())
                monitorMatch = true;
        }
        if (monitorMatch) {
            std::string desc;
            auto ch = message.getChannel();
            if (message.isController()) {
                desc = "CC " + std::to_string(message.getControllerNumber())
                     + " ch" + std::to_string(ch)
                     + " = " + std::to_string(message.getControllerValue());
            } else if (message.isNoteOn()) {
                desc = "Note "
                     + juce::MidiMessage::getMidiNoteName(message.getNoteNumber(), true, true, 3).toStdString()
                     + " ch" + std::to_string(ch);
            } else if (message.isNoteOff()) {
                desc = "NoteOff "
                     + juce::MidiMessage::getMidiNoteName(message.getNoteNumber(), true, true, 3).toStdString()
                     + " ch" + std::to_string(ch);
            } else if (message.isPitchWheel()) {
                desc = "Pitch " + std::to_string(message.getPitchWheelValue())
                     + " ch" + std::to_string(ch);
            } else if (message.isChannelPressure()) {
                desc = "Pressure " + std::to_string(message.getChannelPressureValue())
                     + " ch" + std::to_string(ch);
            }
            if (!desc.empty()) {
                auto cb = deviceMonitorCallback;
                std::string evType;
                int evCh = ch;
                int evNum = 0;
                if (message.isController()) {
                    evType = "cc";
                    evNum = message.getControllerNumber();
                } else if (message.isNoteOn() || message.isNoteOff()) {
                    evType = "note";
                    evNum = message.getNoteNumber();
                } else if (message.isPitchWheel()) {
                    evType = "pitch";
                } else if (message.isChannelPressure()) {
                    evType = "pressure";
                }
                juce::MessageManager::callAsync([cb, desc, evType, evCh, evNum] {
                    cb(desc, evType, evCh, evNum);
                });
            }
        }
    }

    // Global monitor: fire for all devices (debug pane)
    if (globalMonitorCallback) {
        std::string devName = sourcePortName.isNotEmpty() ? sourcePortName.toStdString() : "unknown";
        std::string desc;
        std::string evType;
        int ch = message.getChannel();
        int num = 0;
        int val = 0;

        if (message.isController()) {
            evType = "CC";
            num = message.getControllerNumber();
            val = message.getControllerValue();
            desc = "CC " + std::to_string(num) + " = " + std::to_string(val);
        } else if (message.isNoteOn()) {
            evType = "NoteOn";
            num = message.getNoteNumber();
            val = message.getVelocity();
            desc = "Note " + juce::MidiMessage::getMidiNoteName(num, true, true, 3).toStdString()
                 + " vel=" + std::to_string(val);
        } else if (message.isNoteOff()) {
            evType = "NoteOff";
            num = message.getNoteNumber();
            desc = "Note " + juce::MidiMessage::getMidiNoteName(num, true, true, 3).toStdString() + " off";
        } else if (message.isPitchWheel()) {
            evType = "Pitch";
            val = message.getPitchWheelValue();
            desc = "Pitch " + std::to_string(val);
        } else if (message.isChannelPressure()) {
            evType = "Pressure";
            val = message.getChannelPressureValue();
            desc = "Pressure " + std::to_string(val);
        }

        if (!desc.empty()) {
            auto cb = globalMonitorCallback;
            juce::MessageManager::callAsync([cb, devName, desc, evType, ch, num, val] {
                cb(devName, desc, evType, ch, num, val);
            });
        }
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
