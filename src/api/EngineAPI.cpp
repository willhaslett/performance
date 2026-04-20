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

std::pair<float, float> EngineAPI::getTrackPeakLevelStereo(const juce::String& trackId) {
    return engine.getTrackPeakLevelStereo(trackId);
}

std::pair<float, float> EngineAPI::getBusPeakLevelStereo(const juce::String& busId) {
    return engine.getBusPeakLevelStereo(busId);
}

std::pair<float, float> EngineAPI::getMasterPeakLevelStereo() {
    return engine.getMasterPeakLevelStereo();
}

// --- Plugin UI ---

void EngineAPI::openPluginEditor(const juce::String& parentId, const juce::String& effectId) {
    // Get plugin name from state (SSOT)
    juce::String pluginName;
    if (effectId.isEmpty()) {
        // Instrument
        auto* track = state.findTrack(TrackId{TrackId{parentId.toStdString()}});
        if (track && !track->pluginId.empty()) {
            auto* plugin = state.findPluginById(track->pluginId);
            if (plugin) pluginName = juce::String(plugin->name);
        }
    } else {
        // Effect
        auto* fx = state.findEffect(EffectId{effectId.toStdString()});
        if (fx) {
            auto* plugin = state.findPluginById(fx->pluginId);
            if (plugin) pluginName = juce::String(plugin->name);
        }
    }

    // Build preset callbacks
    AudioEngine::PresetCallbacks callbacks;
    if (pluginName.isNotEmpty()) {
        // Resolve current preset name from state
        PresetId presetId;
        if (effectId.isEmpty()) {
            auto* track = state.findTrack(TrackId{TrackId{parentId.toStdString()}});
            if (track) presetId = track->presetId;
        } else {
            auto* fx = state.findEffect(EffectId{effectId.toStdString()});
            if (fx) presetId = fx->presetId;
        }
        if (!presetId.empty()) {
            auto* preset = state.findPresetById(presetId);
            if (preset) callbacks.currentPresetName = juce::String(preset->name);
        }

        callbacks.listPresets = [this, pluginName]() {
            return listPresets(pluginName);
        };
        callbacks.savePreset = [this, parentId, effectId, pluginName](const juce::String& name) {
            savePreset(parentId, effectId, name);
            // Update presetId in state
            auto* plugin = state.findPluginByName(pluginName.toStdString());
            if (plugin) {
                auto* preset = state.findPreset(plugin->id, name.toStdString());
                if (preset) {
                    if (effectId.isEmpty())
                        state.setTrackPresetId(TrackId{TrackId{parentId.toStdString()}}, preset->id);
                    else
                        state.setEffectPresetId(EffectId{effectId.toStdString()}, preset->id);
                }
            }
        };
        callbacks.loadPreset = [this, parentId, effectId, pluginName](const juce::String& name) {
            loadPreset(parentId, effectId, name);
            // Update presetId in state
            auto* plugin = state.findPluginByName(pluginName.toStdString());
            if (plugin) {
                auto* preset = state.findPreset(plugin->id, name.toStdString());
                if (preset) {
                    if (effectId.isEmpty())
                        state.setTrackPresetId(TrackId{TrackId{parentId.toStdString()}}, preset->id);
                    else
                        state.setEffectPresetId(EffectId{effectId.toStdString()}, preset->id);
                }
            }
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

void EngineAPI::rescanPlugins() {
    engine.scanForPlugins();
}

void EngineAPI::pruneMissingPlugins() {
    auto& known = engine.getKnownPlugins();
    std::vector<juce::PluginDescription> toRemove;
    for (auto& type : known.getTypes()) {
        auto file = juce::File(type.fileOrIdentifier);
        // AU plugins live inside .component bundles; AU system plugins
        // have identifiers that don't resolve to real paths (no file,
        // no .component extension) — leave those alone.
        if (type.fileOrIdentifier.endsWith(".component") && !file.exists()) {
            toRemove.push_back(type);
        }
    }
    for (auto& t : toRemove) {
        known.removeType(t);
    }
    if (!toRemove.empty()) {
        engine.savePluginCache();
    }
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

    // Save parameter snapshot alongside blob for morphing
    auto paramsFile = dir.getChildFile(presetName + ".params.json");
    saveParamSnapshot(paramsFile, proc);

    perfLog("[EngineAPI] Saved preset \"%s\" for %s (%d bytes, %d params)\n",
            presetName.toRawUTF8(), proc->getName().toRawUTF8(),
            (int)memState.getSize(), proc->getNumParameters());
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

// --- Parameter snapshots ---

EngineAPI::ParamSnapshot EngineAPI::captureParams(juce::AudioProcessor* proc) {
    ParamSnapshot snap;
    if (!proc) return snap;
    auto& params = proc->getParameters();
    snap.values.reserve(params.size());
    for (auto* p : params)
        snap.values.push_back(p->getValue());
    return snap;
}

void EngineAPI::applyParams(juce::AudioProcessor* proc, const ParamSnapshot& target,
                             float t, const ParamSnapshot& from) {
    if (!proc) return;
    auto& params = proc->getParameters();
    int n = std::min({ (int)params.size(), (int)from.values.size(), (int)target.values.size() });
    for (int i = 0; i < n; ++i) {
        float v = from.values[i] + t * (target.values[i] - from.values[i]);
        params[i]->setValue(v);
    }
}

void EngineAPI::saveParamSnapshot(const juce::File& file, juce::AudioProcessor* proc) {
    if (!proc) return;
    auto& params = proc->getParameters();

    juce::String json = "[\n";
    for (int i = 0; i < (int)params.size(); ++i) {
        if (i > 0) json += ",\n";
        json += "  {\"index\":" + juce::String(i)
              + ",\"name\":\"" + params[i]->getName(128).replace("\"", "\\\"") + "\""
              + ",\"value\":" + juce::String(params[i]->getValue(), 6) + "}";
    }
    json += "\n]";
    file.replaceWithText(json);
}

EngineAPI::ParamSnapshot EngineAPI::loadParamSnapshot(const juce::File& file) {
    ParamSnapshot snap;
    if (!file.existsAsFile()) return snap;

    auto json = juce::JSON::parse(file.loadFileAsString());
    if (auto* arr = json.getArray()) {
        for (auto& item : *arr) {
            if (auto* obj = item.getDynamicObject()) {
                int index = (int)obj->getProperty("index");
                float value = (float)obj->getProperty("value");
                // Ensure vector is large enough
                while ((int)snap.values.size() <= index)
                    snap.values.push_back(0.0f);
                snap.values[index] = value;
            }
        }
    }
    return snap;
}

EngineAPI::ParamSnapshot EngineAPI::getPresetParams(const juce::String& pluginName,
                                                     const juce::String& presetName) {
    auto file = getPresetsDir().getChildFile(pluginName).getChildFile(presetName + ".params.json");
    return loadParamSnapshot(file);
}
