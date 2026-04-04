#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include <memory>
#include <vector>

class AudioEngine;
class AutomationEngine;
class MIDIEngine;
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
    void addInstrument(const juce::String& trackName, const juce::String& pluginName,
                       const juce::String& snapshotName = "");
    void addTrackEffect(const juce::String& trackName, const juce::String& effectName,
                        const juce::String& pluginName);
    void setTrackMidiEnabled(const juce::String& trackName, bool enabled);
    void setTrackGain(const juce::String& trackName, float gain);
    float getTrackGain(const juce::String& trackName);

    // --- Bus management ---
    void createBus(const juce::String& name);
    void removeBus(const juce::String& name);
    void addBusEffect(const juce::String& busName, const juce::String& effectName,
                      const juce::String& pluginName);
    void setBusGain(const juce::String& busName, float gain);

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
    using Handler = std::function<void(float)>;
    void bind(const juce::String& type, int channel, int number,
              Handler handler, const juce::String& description = "");
    void unbind(const juce::String& type, int channel, int number);
    void unbindAll();

    // --- Automation ---
    using AutomationCallback = std::function<void(float)>;
    using EasingFn = std::function<float(float)>;
    int interpolate(float from, float to, float durationSec,
                    AutomationCallback callback, EasingFn easing = nullptr);
    int delay(float delaySec, std::function<void()> callback);
    void cancelAutomation(int handle);
    void cancelAllAutomation();

    // --- Presets ---
    std::vector<juce::String> listPresets(const juce::String& trackName);
    void loadPreset(const juce::String& trackName, int index);
    void loadPresetByName(const juce::String& trackName, const juce::String& presetName);

    // --- Plugin state snapshots ---
    // Snapshots are stored per plugin: ~/.config/performance/snapshots/<pluginName>/<snapshotName>.state
    void saveSnapshot(const juce::String& trackName, const juce::String& snapshotName);
    void loadSnapshot(const juce::String& trackName, const juce::String& snapshotName);
    std::vector<juce::String> listSnapshots(const juce::String& pluginName);

    // --- Plugin UI ---
    void openPluginEditor(const juce::String& trackName, const juce::String& effectName = "");

    // --- Song management ---
    void loadSong(const SongDef& song);
    void unloadSong();
    bool isSongLoaded() const;
    juce::String getSongName() const;

    // --- Query ---
    std::vector<juce::String> listPlugins() const;
    void log(const juce::String& message);

    // Access for main.cpp window setup (device manager needed for JUCE audio)
    juce::AudioDeviceManager& getDeviceManager();

private:
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<AutomationEngine> automationEngine;
    std::unique_ptr<MIDIEngine> midiEngine;
    std::unique_ptr<SongRuntime> songRuntime;
};
