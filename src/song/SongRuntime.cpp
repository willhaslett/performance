#include "song/SongRuntime.h"
#include "engine/Log.h"

void SongRuntime::unload() {
    controlMap.clear();
}

void SongRuntime::addBinding(const MIDIControl& control, ControlHandler handler,
                              const juce::String& description) {
    controlMap[control].push_back(std::move(handler));
    perfLog("[Song] Binding added: %s%s\n", control.toString().toRawUTF8(),
            description.isNotEmpty() ? (" -> " + description).toRawUTF8() : "");
}

void SongRuntime::removeBinding(const MIDIControl& control) {
    controlMap.erase(control);
}

void SongRuntime::clearBindings() {
    controlMap.clear();
}

void SongRuntime::dispatchControl(const MIDIControl& control, float value) {
    // Exact match (device + channel + number)
    auto it = controlMap.find(control);
    if (it != controlMap.end())
        for (auto& handler : it->second)
            handler(value);

    // Wildcard: any device (if device was specified)
    if (!control.deviceId.empty()) {
        MIDIControl anyDevice = control;
        anyDevice.deviceId.clear();
        auto ait = controlMap.find(anyDevice);
        if (ait != controlMap.end())
            for (auto& handler : ait->second)
                handler(value);
    }

    // Wildcard: any channel (channel 0)
    if (control.channel != 0) {
        MIDIControl anyCh = control;
        anyCh.channel = 0;
        auto wit = controlMap.find(anyCh);
        if (wit != controlMap.end())
            for (auto& handler : wit->second)
                handler(value);

        // Both wildcards: any device + any channel
        if (!control.deviceId.empty()) {
            anyCh.deviceId.clear();
            auto bwit = controlMap.find(anyCh);
            if (bwit != controlMap.end())
                for (auto& handler : bwit->second)
                    handler(value);
        }
    }
}

void SongRuntime::handleControl(const std::string& deviceId, int channel, int ccNumber, int value) {
    dispatchControl({ MIDIControl::CC, channel, ccNumber, deviceId }, value / 127.0f);
}

void SongRuntime::handleNoteOn(const std::string& deviceId, int channel, int noteNumber, int velocity) {
    dispatchControl({ MIDIControl::Note, channel, noteNumber, deviceId }, velocity / 127.0f);
}

void SongRuntime::handleNoteOff(const std::string& deviceId, int channel, int noteNumber) {
    dispatchControl({ MIDIControl::Note, channel, noteNumber, deviceId }, 0.0f);
}

void SongRuntime::handlePitchBend(const std::string& deviceId, int channel, int value) {
    dispatchControl({ MIDIControl::PitchBend, channel, 0, deviceId }, value / 16383.0f);
}

void SongRuntime::handlePressure(const std::string& deviceId, int channel, int value) {
    dispatchControl({ MIDIControl::Pressure, channel, 0, deviceId }, value / 127.0f);
}
