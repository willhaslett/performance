#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

class AudioEngine;
class StateAPI;

// Engine-only concerns: peak levels, processor access, plugin UI, plugin discovery.
// All state reads/writes go through StateAPI. Use this API only when you need
// something the state store can't provide (live DSP processors, transient measurements).

class EngineAPI {
public:
    EngineAPI(AudioEngine& engine, StateAPI& state);

    // --- Peak levels (transient, engine-only) ---
    float getTrackPeakLevel(const juce::String& trackId);
    float getBusPeakLevel(const juce::String& busId);
    float getMasterPeakLevel();
    std::pair<float, float> getTrackPeakLevelStereo(const juce::String& trackId);
    std::pair<float, float> getBusPeakLevelStereo(const juce::String& busId);
    std::pair<float, float> getMasterPeakLevelStereo();

    // --- Plugin UI ---
    void openPluginEditor(const juce::String& parentId, const juce::String& effectId = "");
    void closeTopPluginEditor();

    // --- Plugin discovery ---
    std::vector<juce::String> listPlugins() const;
    std::vector<juce::String> listInstrumentPlugins() const;
    std::vector<juce::String> listEffectPlugins() const;

    // --- Processor access (for presets, params) ---
    juce::AudioProcessor* getTrackInstrumentProcessor(const juce::String& trackId);
    juce::AudioProcessor* getEffectProcessor(const juce::String& parentId,
                                              const juce::String& effectId);

    // --- Parameters (direct processor access) ---
    void setParam(const juce::String& trackId, const juce::String& paramName, float value);
    float getParam(const juce::String& trackId, const juce::String& paramName);
    void setEffectParam(const juce::String& parentId, const juce::String& effectId,
                        const juce::String& paramName, float value);
    float getEffectParam(const juce::String& parentId, const juce::String& effectId,
                         const juce::String& paramName);

    // --- Presets (engine processor state + file I/O) ---
    // For instruments: effectId is empty. For effects: effectId is the effect UUID.
    void savePreset(const juce::String& parentId, const juce::String& effectId,
                    const juce::String& presetName);
    void loadPreset(const juce::String& parentId, const juce::String& effectId,
                    const juce::String& presetName);
    std::vector<juce::String> listPresets(const juce::String& pluginName);

    // --- Parameter snapshots & morphing ---
    struct ParamSnapshot {
        std::vector<float> values;  // indexed by parameter index, normalized 0-1
    };
    static ParamSnapshot captureParams(juce::AudioProcessor* proc);
    static void applyParams(juce::AudioProcessor* proc, const ParamSnapshot& snapshot, float t,
                             const ParamSnapshot& from);
    static void saveParamSnapshot(const juce::File& file, juce::AudioProcessor* proc);
    static ParamSnapshot loadParamSnapshot(const juce::File& file);
    ParamSnapshot getPresetParams(const juce::String& pluginName, const juce::String& presetName);

    // --- Audio inputs ---
    std::vector<juce::String> getInputChannelNames() const;
    std::vector<float> getInputPeakLevels() const;

    // --- Device ---
    juce::AudioDeviceManager& getDeviceManager();
    AudioEngine& getAudioEngine() { return engine; }

private:
    AudioEngine& engine;
    StateAPI& state;

    // Map songId → "Output" for engine's master output concept
    juce::String engineParentId(const juce::String& parentId) const;

    static juce::File getPresetsDir();
};
