#include "song/SongRuntime.h"
#include "engine/AudioEngine.h"

SongRuntime::SongRuntime(AudioEngine& eng) : engine(eng) {}

void SongRuntime::load(const SongDef& song) {
    unload();

    songName = song.name;
    DBG("Loading song: " + songName);

    // Load instruments and their effect chains
    int instrumentsLoaded = 0;
    for (auto& inst : song.instruments) {
        DBG("  Instrument: " + inst.name + " (" + inst.pluginName + ")");
        if (engine.loadInstrument(inst.pluginName)) {
            instrumentsLoaded++;

            for (auto& fx : inst.effects) {
                DBG("    Effect: " + fx.name + " (" + fx.pluginName + ")");
                engine.loadEffect(fx.pluginName);
            }
        }
    }

    // Build control map
    for (auto& binding : song.bindings) {
        controlMap[binding.control].push_back(binding.handler);
        DBG("  Binding: " + binding.control.toString() +
            (binding.description.isNotEmpty() ? " -> " + binding.description : ""));
    }

    loaded = true;
    DBG("Song loaded: " + songName + " (" + juce::String(instrumentsLoaded) + " instruments, " +
        juce::String(song.bindings.size()) + " bindings)");
}

void SongRuntime::unload() {
    controlMap.clear();
    loaded = false;
    songName = "";
}

void SongRuntime::dispatchControl(const MIDIControl& control, float value) {
    // Try exact match (with channel)
    auto it = controlMap.find(control);
    if (it != controlMap.end()) {
        for (auto& handler : it->second)
            handler(value);
    }

    // Try wildcard channel (channel 0 = any)
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

juce::AudioProcessorParameter* SongRuntime::findParam(const juce::String& instrumentName,
                                                        const juce::String& effectName,
                                                        const juce::String& paramName) {
    // For now, search the currently loaded instrument's parameters
    auto* processor = engine.getLoadedProcessor();
    if (!processor) return nullptr;

    for (auto* param : processor->getParameters()) {
        if (param->getName(128).containsIgnoreCase(paramName))
            return param;
    }

    return nullptr;
}
