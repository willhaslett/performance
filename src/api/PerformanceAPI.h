#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class AudioEngine;
class AutomationEngine;
class EngineSync;
class MIDIEngine;
class Registry;
class SongRuntime;
struct SongDef;

class PerformanceAPI {
public:
    PerformanceAPI();
    ~PerformanceAPI();

    void initialise();
    void shutdown();

    // --- Track management ---
    void createTrack(const juce::String& name);
    void removeTrack(const juce::String& name);
    void renameTrack(const juce::String& oldName, const juce::String& newName);
    void addInstrument(const juce::String& trackName, const juce::String& pluginName,
                       const juce::String& presetName = "");
    void removeInstrument(const juce::String& trackName);
    // Effects — unified for tracks and busses
    void addEffect(const juce::String& parentName, const juce::String& effectName,
                   const juce::String& pluginName);
    void removeEffect(const juce::String& parentName, const juce::String& effectName);
    void setTrackMidiEnabled(const juce::String& trackName, bool enabled);
    void setTrackGain(const juce::String& trackName, float gain);
    float getTrackGain(const juce::String& trackName);
    float getTrackPeakLevel(const juce::String& trackName);

    // --- Bus management ---
    void createBus(const juce::String& name);
    void removeBus(const juce::String& name);
    void renameBus(const juce::String& oldName, const juce::String& newName);
    void setBusGain(const juce::String& busName, float gain);

    // --- Master output ---
    void setMasterGain(float gain);
    float getMasterGain();
    float getMasterPeakLevel();

    // --- Sends ---
    void addSend(const juce::String& trackName, const juce::String& busName, float gain = 1.0f);
    void setSendGain(const juce::String& trackName, const juce::String& busName, float gain);

    // --- Parameters ---
    void setParam(const juce::String& trackName, const juce::String& paramName, float value);
    void setEffectParam(const juce::String& trackName, const juce::String& effectName,
                        const juce::String& paramName, float value);
    float getParam(const juce::String& trackName, const juce::String& paramName);
    float getEffectParam(const juce::String& trackName, const juce::String& effectName,
                         const juce::String& paramName);

    // --- MIDI control binding ---
    // Binds a MIDI control to a named action with arguments
    void bind(const juce::String& controlType, int channel, int number,
              const juce::String& actionName, const juce::String& argsJson = "[]",
              const juce::String& description = "");
    void unbind(const juce::String& controlType, int channel, int number);
    void unbindAll();

    // --- Automation ---
    using AutomationCallback = std::function<void(float)>;
    using EasingFn = std::function<float(float)>;
    int interpolate(float from, float to, float durationSec,
                    AutomationCallback callback, EasingFn easing = nullptr);
    int delay(float delaySec, std::function<void()> callback);
    void cancelAutomation(int handle);
    void cancelAllAutomation();

    // --- Presets (plugin state) ---
    // Presets are stored per plugin: ~/.config/performance/snapshots/<pluginName>/<presetName>.state
    void savePreset(const juce::String& trackName, const juce::String& presetName);
    void loadPreset(const juce::String& trackName, const juce::String& presetName);
    std::vector<juce::String> listPresets(const juce::String& pluginName);

    // --- Track presets ---
    void saveTrackPreset(const juce::String& trackName, const juce::String& presetName);
    void loadTrackPreset(const juce::String& trackName, const juce::String& presetName);
    std::vector<juce::String> listTrackPresets();

    // --- Plugin UI ---
    void openPluginEditor(const juce::String& trackName, const juce::String& effectName = "");
    void closeTopPluginEditor();

    // --- Song management ---
    std::string createSong(const juce::String& name);
    void loadSong(const SongDef& song);
    void loadSongFromRegistry(const std::string& songId);
    bool restoreSession();
    void unloadSong();
    bool isSongLoaded() const;
    juce::String getSongName() const;

    // Song persistence — initial state + score
    void saveInitialState();                        // capture current registry state as song's initial state
    void loadInitialState();                        // restore initial state into registry + engine
    void saveScore(const juce::String& scoreJson);
    juce::String getScore() const;
    void replayScore(int upToStep = -1);  // -1 = all steps

    // Query current state as a SongDef (for serialization)
    SongDef getCurrentSongDef() const;
    std::string getCurrentSongId() const { return currentSongId; }

    // --- Generic Registry CRUD ---
    std::string registryCreate(const std::string& type, const std::map<std::string, std::string>& fields);
    std::map<std::string, std::string> registryGet(const std::string& id);
    std::vector<std::map<std::string, std::string>> registryList(const std::string& type,
        const std::map<std::string, std::string>& filters = {});
    void registryUpdate(const std::string& id, const std::map<std::string, std::string>& fields);
    void registryDelete(const std::string& id);

    // --- Query ---
    std::vector<juce::String> listPlugins() const;
    std::vector<juce::String> listInstrumentPlugins() const;
    std::vector<juce::String> listEffectPlugins() const;
    std::vector<juce::String> listTrackNames() const;
    std::vector<juce::String> listBusNames() const;
    juce::String getTrackPluginName(const juce::String& trackName) const;

    struct EffectSlotInfo {
        juce::String effectId;    // internal key for lookups
        juce::String pluginName;  // display name
    };
    std::vector<EffectSlotInfo> getTrackEffects(const juce::String& trackName) const;
    std::vector<EffectSlotInfo> getBusEffects(const juce::String& busName) const;
    std::vector<EffectSlotInfo> getMasterEffects();

    bool isTrackMidiEnabled(const juce::String& trackName) const;

    // Send info for a track
    struct TrackSendInfo {
        juce::String busName;
        float gain;
        float peakLevel;
    };
    std::vector<TrackSendInfo> getTrackSends(const juce::String& trackName) const;
    float getBusGain(const juce::String& busName);
    float getBusPeakLevel(const juce::String& busName);
    void log(const juce::String& message);

    // Access for main.cpp window setup (device manager needed for JUCE audio)
    juce::AudioDeviceManager& getDeviceManager();
    Registry& getRegistry() { return *registry; }

private:
    std::unique_ptr<Registry> registry;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<EngineSync> engineSync;
    std::unique_ptr<AutomationEngine> automationEngine;
    std::unique_ptr<MIDIEngine> midiEngine;
    std::unique_ptr<SongRuntime> songRuntime;

    void populatePluginRegistry();
    void registerBuiltinActions();
    void executeAction(const std::string& actionName, const juce::var& args, float value);
    juce::String resolveArgsToIds(const juce::String& actionName, const juce::String& argsJson);
    juce::String resolveIdToName(const juce::String& id);

    std::string currentSongId;  // active song in registry
};
