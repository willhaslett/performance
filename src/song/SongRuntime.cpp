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
    auto it = controlMap.find(control);
    if (it != controlMap.end()) {
        for (auto& handler : it->second)
            handler(value);
    }

    // Also check wildcard (channel 0 = any)
    if (control.channel != 0) {
        MIDIControl wildcard = control;
        wildcard.channel = 0;
        auto wit = controlMap.find(wildcard);
        if (wit != controlMap.end()) {
            for (auto& handler : wit->second)
                handler(value);
        }
    }
}

void SongRuntime::handleControl(int channel, int ccNumber, int value) {
    dispatchControl({ MIDIControl::CC, channel, ccNumber }, value / 127.0f);
}

void SongRuntime::handleNoteOn(int channel, int noteNumber, int velocity) {
    dispatchControl({ MIDIControl::Note, channel, noteNumber }, velocity / 127.0f);
}

void SongRuntime::handleNoteOff(int channel, int noteNumber) {
    dispatchControl({ MIDIControl::Note, channel, noteNumber }, 0.0f);
}

void SongRuntime::handlePitchBend(int channel, int value) {
    dispatchControl({ MIDIControl::PitchBend, channel, 0 }, value / 16383.0f);
}

void SongRuntime::handlePressure(int channel, int value) {
    dispatchControl({ MIDIControl::Pressure, channel, 0 }, value / 127.0f);
}
