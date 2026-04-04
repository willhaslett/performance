#include "song/SongRuntime.h"
#include "engine/AudioEngine.h"

SongRuntime::SongRuntime(AudioEngine& eng) : engine(eng) {}

void SongRuntime::load(const SongDef& song) {
    unload();

    songName = song.name;
    DBG("Loading song: " + songName);

    for (auto& inst : song.instruments) {
        engine.createChain(inst.name);
        engine.addInstrument(inst.name, inst.pluginName);
        for (auto& fx : inst.effects)
            engine.addEffect(inst.name, fx.name, fx.pluginName);
    }

    // Build control map
    for (auto& binding : song.bindings) {
        controlMap[binding.control].push_back(binding.handler);
        DBG("  Binding: " + binding.control.toString() +
            (binding.description.isNotEmpty() ? " -> " + binding.description : ""));
    }

    loaded = true;
    DBG("Song loaded: " + songName + " (" + juce::String(song.instruments.size()) + " instruments, " +
        juce::String(song.bindings.size()) + " bindings)");
}

void SongRuntime::unload() {
    engine.clearAllChains();
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
    juce::AudioProcessor* processor = nullptr;
    if (effectName.isEmpty())
        processor = engine.getInstrumentProcessor(instrumentName);
    else
        processor = engine.getEffectProcessor(instrumentName, effectName);

    if (!processor) return nullptr;

    for (auto* param : processor->getParameters()) {
        if (param->getName(128).containsIgnoreCase(paramName))
            return param;
    }

    return nullptr;
}
