#include "song/SongRuntime.h"
#include "engine/AudioEngine.h"
#include "engine/Log.h"

SongRuntime::SongRuntime(AudioEngine& eng) : engine(eng) {}

void SongRuntime::load(const SongDef& song) {
    unload();

    songName = song.name;
    perfLog("[Song] Loading song: %s\n", songName.toRawUTF8());

    // Create busses first so sends can reference them
    for (auto& busDef : song.busses) {
        engine.createBus(busDef.name);
        for (auto& fx : busDef.effects)
            engine.addEffect(busDef.name, fx.name, fx.pluginName);
        engine.setBusGain(busDef.name, busDef.outputGain);
    }

    // Create tracks
    for (auto& trackDef : song.tracks) {
        engine.createTrack(trackDef.name);

        auto trackName = trackDef.name;
        engine.addTrackInstrument(trackDef.name, trackDef.pluginName, [this, trackName] {
            engine.openPluginEditor(trackName);
        });

        for (auto& fx : trackDef.effects)
            engine.addEffect(trackDef.name, fx.name, fx.pluginName);

        for (auto& send : trackDef.sends)
            engine.addSend(trackDef.name, send.busName, send.gain);

        engine.setTrackGain(trackDef.name, trackDef.outputGain);

        if (!trackDef.midiEnabled)
            engine.setTrackMidiEnabled(trackDef.name, false);
    }

    // Build control map
    for (auto& binding : song.bindings) {
        controlMap[binding.control].push_back(binding.handler);
        perfLog("[Song]   Binding: %s%s\n", binding.control.toString().toRawUTF8(),
                binding.description.isNotEmpty()
                    ? (" -> " + binding.description).toRawUTF8() : "");
    }

    loaded = true;
    perfLog("[Song] Song loaded: %s (%d tracks, %d busses, %d bindings)\n",
            songName.toRawUTF8(), (int)song.tracks.size(),
            (int)song.busses.size(), (int)song.bindings.size());
}

void SongRuntime::unload() {
    engine.clearAllTracks();
    engine.clearAllBusses();
    controlMap.clear();
    loaded = false;
    songName = "";
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

juce::AudioProcessorParameter* SongRuntime::findParam(const juce::String& trackName,
                                                        const juce::String& effectName,
                                                        const juce::String& paramName) {
    juce::AudioProcessor* processor = nullptr;
    if (effectName.isEmpty())
        processor = engine.getTrackInstrumentProcessor(trackName);
    else
        processor = engine.getTrackEffectProcessor(trackName, effectName);

    if (!processor) return nullptr;

    for (auto* param : processor->getParameters()) {
        if (param->getName(128).containsIgnoreCase(paramName))
            return param;
    }

    return nullptr;
}
