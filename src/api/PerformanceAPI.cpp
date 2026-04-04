#include "api/PerformanceAPI.h"
#include "automation/AutomationEngine.h"
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

    automationEngine = std::make_unique<AutomationEngine>();
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

void PerformanceAPI::addInstrument(const juce::String& trackName, const juce::String& pluginName,
                                    const juce::String& snapshotName) {
    audioEngine->addTrackInstrument(trackName, pluginName, [this, trackName, snapshotName] {
        if (snapshotName.isNotEmpty())
            loadSnapshot(trackName, snapshotName);
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

float PerformanceAPI::getTrackGain(const juce::String& trackName) {
    return audioEngine->getTrackGain(trackName);
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

// --- Presets ---

std::vector<juce::String> PerformanceAPI::listPresets(const juce::String& trackName) {
    std::vector<juce::String> presets;
    if (auto* proc = audioEngine->getTrackInstrumentProcessor(trackName)) {
        for (int i = 0; i < proc->getNumPrograms(); ++i)
            presets.push_back(proc->getProgramName(i));
    }
    return presets;
}

void PerformanceAPI::loadPreset(const juce::String& trackName, int index) {
    // Send MIDI program change — more widely supported than JUCE's setCurrentProgram
    auto msg = juce::MidiMessage::programChange(1, index);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    audioEngine->injectMidi(msg);
    perfLog("[API] Sent program change %d for track \"%s\"\n", index, trackName.toRawUTF8());
}

void PerformanceAPI::loadPresetByName(const juce::String& trackName, const juce::String& presetName) {
    if (auto* proc = audioEngine->getTrackInstrumentProcessor(trackName)) {
        for (int i = 0; i < proc->getNumPrograms(); ++i) {
            if (proc->getProgramName(i).containsIgnoreCase(presetName)) {
                proc->setCurrentProgram(i);
                perfLog("[API] Loaded preset \"%s\" (%d) on track \"%s\"\n",
                        proc->getProgramName(i).toRawUTF8(), i, trackName.toRawUTF8());
                return;
            }
        }
        perfLog("[API] Preset not found: \"%s\" on track \"%s\"\n",
                presetName.toRawUTF8(), trackName.toRawUTF8());
    }
}

// --- Plugin state snapshots ---

static juce::File getSnapshotsDir() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".config/performance/snapshots");
}

void PerformanceAPI::saveSnapshot(const juce::String& trackName, const juce::String& snapshotName) {
    auto* proc = audioEngine->getTrackInstrumentProcessor(trackName);
    if (!proc) {
        perfLog("[API] Cannot save snapshot: no instrument on track \"%s\"\n", trackName.toRawUTF8());
        return;
    }

    juce::MemoryBlock state;
    proc->getStateInformation(state);

    auto dir = getSnapshotsDir().getChildFile(proc->getName());
    dir.createDirectory();
    auto file = dir.getChildFile(snapshotName + ".state");
    file.replaceWithData(state.getData(), state.getSize());

    perfLog("[API] Saved snapshot \"%s\" for %s (%d bytes)\n",
            snapshotName.toRawUTF8(), proc->getName().toRawUTF8(), (int)state.getSize());
}

void PerformanceAPI::loadSnapshot(const juce::String& trackName, const juce::String& snapshotName) {
    auto* proc = audioEngine->getTrackInstrumentProcessor(trackName);
    if (!proc) {
        perfLog("[API] Cannot load snapshot: no instrument on track \"%s\"\n", trackName.toRawUTF8());
        return;
    }

    auto file = getSnapshotsDir().getChildFile(proc->getName()).getChildFile(snapshotName + ".state");
    if (!file.existsAsFile()) {
        perfLog("[API] Snapshot not found: \"%s\" for %s\n",
                snapshotName.toRawUTF8(), proc->getName().toRawUTF8());
        return;
    }

    juce::MemoryBlock state;
    file.loadFileAsData(state);
    proc->setStateInformation(state.getData(), (int)state.getSize());

    perfLog("[API] Loaded snapshot \"%s\" for %s\n",
            snapshotName.toRawUTF8(), proc->getName().toRawUTF8());
}

std::vector<juce::String> PerformanceAPI::listSnapshots(const juce::String& pluginName) {
    std::vector<juce::String> names;
    auto dir = getSnapshotsDir().getChildFile(pluginName);
    if (!dir.isDirectory()) return names;

    for (auto& entry : juce::RangedDirectoryIterator(dir, false, "*.state")) {
        names.push_back(entry.getFile().getFileNameWithoutExtension());
    }
    std::sort(names.begin(), names.end());
    return names;
}

// --- Automation ---

int PerformanceAPI::interpolate(float from, float to, float durationSec,
                                 AutomationCallback callback, EasingFn easing) {
    return automationEngine->interpolate(from, to, durationSec, std::move(callback), std::move(easing));
}

int PerformanceAPI::delay(float delaySec, std::function<void()> callback) {
    return automationEngine->delay(delaySec, std::move(callback));
}

void PerformanceAPI::cancelAutomation(int handle) {
    automationEngine->cancel(handle);
}

void PerformanceAPI::cancelAllAutomation() {
    automationEngine->cancelAll();
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
