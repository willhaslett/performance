#include "api/EngineAPI.h"
#include "api/StateAPI.h"
#include "engine/AudioEngine.h"
#include "engine/Log.h"

EngineAPI::EngineAPI(AudioEngine& engine, StateAPI& state)
    : engine(engine), state(state) {}

// --- Helpers ---

juce::String EngineAPI::engineParentId(const juce::String& parentId) const {
    return (parentId.toStdString() == state.getMasterOutputId()) ? juce::String("Output") : parentId;
}

juce::File EngineAPI::getPresetsDir() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".config/performance/snapshots");
}

// --- Peak levels ---

float EngineAPI::getTrackPeakLevel(const juce::String& trackId) {
    return engine.getTrackPeakLevel(trackId);
}

float EngineAPI::getBusPeakLevel(const juce::String& busId) {
    return engine.getBusPeakLevel(busId);
}

float EngineAPI::getMasterPeakLevel() {
    return engine.getMasterPeakLevel();
}

// --- Plugin UI ---

void EngineAPI::openPluginEditor(const juce::String& parentId, const juce::String& effectId) {
    // Get plugin name from state (SSOT)
    juce::String pluginName;
    if (effectId.isEmpty()) {
        // Instrument
        auto* track = state.findTrack(parentId.toStdString());
        if (track && !track->pluginId.empty()) {
            auto* plugin = state.findPluginById(track->pluginId);
            if (plugin) pluginName = juce::String(plugin->name);
        }
    } else {
        // Effect
        auto* fx = state.findEffect(effectId.toStdString());
        if (fx) {
            auto* plugin = state.findPluginById(fx->pluginId);
            if (plugin) pluginName = juce::String(plugin->name);
        }
    }

    // Build preset callbacks
    AudioEngine::PresetCallbacks callbacks;
    if (pluginName.isNotEmpty()) {
        callbacks.listPresets = [this, pluginName]() {
            return listPresets(pluginName);
        };
        callbacks.savePreset = [this, parentId, effectId](const juce::String& name) {
            savePreset(parentId, effectId, name);
        };
        callbacks.loadPreset = [this, parentId, effectId](const juce::String& name) {
            loadPreset(parentId, effectId, name);
        };
    }

    auto engParent = engineParentId(parentId);
    auto title = pluginName.isNotEmpty() ? pluginName : juce::String("Plugin");
    engine.openPluginEditor(engParent, effectId, title, std::move(callbacks));
}

void EngineAPI::closeTopPluginEditor() {
    engine.closeTopPluginEditor();
}

// --- Plugin discovery ---

std::vector<juce::String> EngineAPI::listPlugins() const {
    std::vector<juce::String> names;
    for (auto& type : engine.getKnownPlugins().getTypes())
        names.push_back(type.name);
    return names;
}

std::vector<juce::String> EngineAPI::listInstrumentPlugins() const {
    std::vector<juce::String> names;
    for (auto& type : engine.getKnownPlugins().getTypes())
        if (type.isInstrument)
            names.push_back(type.name);
    return names;
}

std::vector<juce::String> EngineAPI::listEffectPlugins() const {
    std::vector<juce::String> names;
    for (auto& type : engine.getKnownPlugins().getTypes())
        if (!type.isInstrument)
            names.push_back(type.name);
    return names;
}

// --- Processor access ---

juce::AudioProcessor* EngineAPI::getTrackInstrumentProcessor(const juce::String& trackId) {
    return engine.getTrackInstrumentProcessor(trackId);
}

juce::AudioProcessor* EngineAPI::getEffectProcessor(const juce::String& parentId,
                                                     const juce::String& effectId) {
    return engine.getEffectProcessor(engineParentId(parentId), effectId);
}

// --- Parameters ---

static juce::AudioProcessorParameter* findParam(AudioEngine& engine,
                                                  const juce::String& parentId,
                                                  const juce::String& effectId,
                                                  const juce::String& paramName) {
    juce::AudioProcessor* proc = nullptr;
    if (effectId.isEmpty())
        proc = engine.getTrackInstrumentProcessor(parentId);
    else
        proc = engine.getEffectProcessor(parentId, effectId);
    if (!proc) return nullptr;

    for (auto* param : proc->getParameters())
        if (param->getName(128) == paramName)
            return param;
    return nullptr;
}

void EngineAPI::setParam(const juce::String& trackId, const juce::String& paramName, float value) {
    if (auto* param = findParam(engine, trackId, "", paramName))
        param->setValueNotifyingHost(value);
}

float EngineAPI::getParam(const juce::String& trackId, const juce::String& paramName) {
    if (auto* param = findParam(engine, trackId, "", paramName))
        return param->getValue();
    return 0.0f;
}

void EngineAPI::setEffectParam(const juce::String& parentId, const juce::String& effectId,
                                const juce::String& paramName, float value) {
    if (auto* param = findParam(engine, engineParentId(parentId), effectId, paramName))
        param->setValueNotifyingHost(value);
}

float EngineAPI::getEffectParam(const juce::String& parentId, const juce::String& effectId,
                                 const juce::String& paramName) {
    if (auto* param = findParam(engine, engineParentId(parentId), effectId, paramName))
        return param->getValue();
    return 0.0f;
}

// --- Presets ---

void EngineAPI::savePreset(const juce::String& parentId, const juce::String& effectId,
                            const juce::String& presetName) {
    juce::AudioProcessor* proc = nullptr;
    if (effectId.isEmpty())
        proc = engine.getTrackInstrumentProcessor(parentId);
    else
        proc = engine.getEffectProcessor(engineParentId(parentId), effectId);

    if (!proc) {
        perfLog("[EngineAPI] Cannot save preset: no processor\n");
        return;
    }

    juce::MemoryBlock memState;
    proc->getStateInformation(memState);

    auto dir = getPresetsDir().getChildFile(proc->getName());
    dir.createDirectory();
    auto file = dir.getChildFile(presetName + ".state");
    file.replaceWithData(memState.getData(), memState.getSize());

    // Register in state catalog
    auto* plugin = state.findPluginByName(proc->getName().toStdString());
    if (plugin) {
        auto kind = effectId.isEmpty() ? PresetKind::Instrument : PresetKind::Effect;
        state.createPreset(plugin->id, presetName.toStdString(),
                           file.getFullPathName().toStdString(), kind);
    }

    perfLog("[EngineAPI] Saved preset \"%s\" for %s (%d bytes)\n",
            presetName.toRawUTF8(), proc->getName().toRawUTF8(), (int)memState.getSize());
}

void EngineAPI::loadPreset(const juce::String& parentId, const juce::String& effectId,
                            const juce::String& presetName) {
    juce::AudioProcessor* proc = nullptr;
    if (effectId.isEmpty())
        proc = engine.getTrackInstrumentProcessor(parentId);
    else
        proc = engine.getEffectProcessor(engineParentId(parentId), effectId);

    if (!proc) {
        perfLog("[EngineAPI] Cannot load preset: no processor\n");
        return;
    }

    auto file = getPresetsDir().getChildFile(proc->getName()).getChildFile(presetName + ".state");
    if (!file.existsAsFile()) return;

    juce::MemoryBlock memState;
    file.loadFileAsData(memState);
    proc->setStateInformation(memState.getData(), (int)memState.getSize());

    perfLog("[EngineAPI] Loaded preset \"%s\" for %s\n",
            presetName.toRawUTF8(), proc->getName().toRawUTF8());
}

std::vector<juce::String> EngineAPI::listPresets(const juce::String& pluginName) {
    std::vector<juce::String> names;
    auto dir = getPresetsDir().getChildFile(pluginName);
    if (!dir.isDirectory()) return names;

    for (auto& entry : juce::RangedDirectoryIterator(dir, false, "*.state"))
        names.push_back(entry.getFile().getFileNameWithoutExtension());

    std::sort(names.begin(), names.end());
    return names;
}

// --- Device ---

std::vector<juce::String> EngineAPI::getInputChannelNames() const {
    return engine.getInputChannelNames();
}

std::vector<float> EngineAPI::getInputPeakLevels() const {
    return engine.getInputPeakLevels();
}

juce::AudioDeviceManager& EngineAPI::getDeviceManager() {
    return engine.getDeviceManager();
}
