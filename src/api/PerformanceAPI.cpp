#include "api/PerformanceAPI.h"
#include "engine/AudioEngine.h"
#include "engine/MIDIEngine.h"
#include "engine/Log.h"
#include "song/Song.h"
#include "song/SongRuntime.h"

PerformanceAPI::PerformanceAPI() {}

PerformanceAPI::~PerformanceAPI() {
    shutdown();
}

void PerformanceAPI::initialise() {
    audioEngine = std::make_unique<AudioEngine>();
    audioEngine->initialise();
    perfLog("[API] AudioEngine initialised\n");

    songRuntime = std::make_unique<SongRuntime>(*audioEngine);

    midiEngine = std::make_unique<MIDIEngine>(
        audioEngine->getDeviceManager(), *audioEngine);
    midiEngine->setSongRuntime(songRuntime.get());
    midiEngine->setMonitorMode(true);
    midiEngine->initialise();
    perfLog("[API] MIDIEngine initialised\n");
}

void PerformanceAPI::shutdown() {
    songRuntime.reset();
    midiEngine.reset();
    audioEngine.reset();
}

// --- Track management ---

void PerformanceAPI::createTrack(const juce::String& name) {
    audioEngine->createTrack(name);
}

void PerformanceAPI::removeTrack(const juce::String& name) {
    audioEngine->removeTrack(name);
}

void PerformanceAPI::addInstrument(const juce::String& trackName, const juce::String& pluginName) {
    audioEngine->addTrackInstrument(trackName, pluginName, [this, trackName] {
        audioEngine->openPluginEditor(trackName);
    });
}

void PerformanceAPI::addTrackEffect(const juce::String& trackName, const juce::String& effectName,
                                     const juce::String& pluginName) {
    audioEngine->addTrackEffect(trackName, effectName, pluginName);
}

void PerformanceAPI::setTrackMidiEnabled(const juce::String& trackName, bool enabled) {
    audioEngine->setTrackMidiEnabled(trackName, enabled);
}

void PerformanceAPI::setTrackGain(const juce::String& trackName, float gain) {
    audioEngine->setTrackGain(trackName, gain);
}

// --- Bus management ---

void PerformanceAPI::createBus(const juce::String& name) {
    audioEngine->createBus(name);
}

void PerformanceAPI::removeBus(const juce::String& name) {
    audioEngine->removeBus(name);
}

void PerformanceAPI::addBusEffect(const juce::String& busName, const juce::String& effectName,
                                   const juce::String& pluginName) {
    audioEngine->addBusEffect(busName, effectName, pluginName);
}

void PerformanceAPI::setBusGain(const juce::String& busName, float gain) {
    audioEngine->setBusGain(busName, gain);
}

// --- Sends ---

void PerformanceAPI::addSend(const juce::String& trackName, const juce::String& busName, float gain) {
    audioEngine->addSend(trackName, busName, gain);
}

void PerformanceAPI::setSendGain(const juce::String& trackName, const juce::String& busName, float gain) {
    audioEngine->setSendGain(trackName, busName, gain);
}

// --- Parameters ---

static juce::AudioProcessorParameter* findParamInternal(SongRuntime& runtime,
                                                          const juce::String& trackName,
                                                          const juce::String& effectName,
                                                          const juce::String& paramName) {
    return runtime.findParam(trackName, effectName, paramName);
}

void PerformanceAPI::setParam(const juce::String& trackName, const juce::String& paramName, float value) {
    if (auto* param = findParamInternal(*songRuntime, trackName, "", paramName))
        param->setValueNotifyingHost(value);
}

void PerformanceAPI::setEffectParam(const juce::String& trackName, const juce::String& effectName,
                                     const juce::String& paramName, float value) {
    if (auto* param = findParamInternal(*songRuntime, trackName, effectName, paramName))
        param->setValueNotifyingHost(value);
}

float PerformanceAPI::getParam(const juce::String& trackName, const juce::String& paramName) {
    if (auto* param = findParamInternal(*songRuntime, trackName, "", paramName))
        return param->getValue();
    return 0.0f;
}

float PerformanceAPI::getEffectParam(const juce::String& trackName, const juce::String& effectName,
                                      const juce::String& paramName) {
    if (auto* param = findParamInternal(*songRuntime, trackName, effectName, paramName))
        return param->getValue();
    return 0.0f;
}

// --- MIDI control binding ---

static MIDIControl::Type parseControlType(const juce::String& type) {
    if (type.equalsIgnoreCase("cc")) return MIDIControl::CC;
    if (type.equalsIgnoreCase("note")) return MIDIControl::Note;
    if (type.equalsIgnoreCase("pitchbend")) return MIDIControl::PitchBend;
    if (type.equalsIgnoreCase("pressure")) return MIDIControl::Pressure;
    return MIDIControl::CC;
}

void PerformanceAPI::bind(const juce::String& type, int channel, int number,
                           Handler handler, const juce::String& description) {
    MIDIControl control = { parseControlType(type), channel, number };
    songRuntime->addBinding(control, std::move(handler), description);
}

void PerformanceAPI::unbind(const juce::String& type, int channel, int number) {
    MIDIControl control = { parseControlType(type), channel, number };
    songRuntime->removeBinding(control);
}

void PerformanceAPI::unbindAll() {
    songRuntime->clearBindings();
}

// --- Plugin UI ---

void PerformanceAPI::openPluginEditor(const juce::String& trackName, const juce::String& effectName) {
    audioEngine->openPluginEditor(trackName, effectName);
}

// --- Song management ---

void PerformanceAPI::loadSong(const SongDef& song) {
    songRuntime->load(song);
}

void PerformanceAPI::unloadSong() {
    songRuntime->unload();
}

bool PerformanceAPI::isSongLoaded() const {
    return songRuntime->isLoaded();
}

juce::String PerformanceAPI::getSongName() const {
    return songRuntime->getSongName();
}

// --- Query ---

std::vector<juce::String> PerformanceAPI::listPlugins() const {
    std::vector<juce::String> names;
    for (auto& type : audioEngine->getKnownPlugins().getTypes())
        names.push_back(type.name);
    return names;
}

void PerformanceAPI::log(const juce::String& message) {
    perfLog("[API] %s\n", message.toRawUTF8());
}

juce::AudioDeviceManager& PerformanceAPI::getDeviceManager() {
    return audioEngine->getDeviceManager();
}
