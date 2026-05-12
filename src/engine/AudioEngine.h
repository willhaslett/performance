#pragma once
#include "engine/AudioEngineInterface.h"
#include "engine/GraphWrapper.h"
#include "engine/MidiSourceNode.h"
#include "engine/RecordFIFO.h"
#include "engine/AudioRecordFIFO.h"
#include "engine/AudioWriterThread.h"
#include "engine/AudioFileNode.h"
#include "state/StateModel.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <AudioToolbox/AudioToolbox.h>
#include <functional>
#include <map>

class GainProcessor;
class Arrangement;

class AudioEngine : public AudioEngineInterface,
                     private juce::ChangeListener {
public:
    AudioEngine();
    ~AudioEngine();

    void initialise();
    void shutdown();

    // Plugin scanning + cache
    bool needsPluginScan() const { return pluginScanNeeded; }
    void scanForPlugins();
    void scanComponentDirectory(const juce::File& directory);
    bool registerComponent(const juce::String& pluginName);
    void listAvailablePlugins() const;
    bool loadPluginCache();
    void savePluginCache();

    // Track management
    juce::String createTrack(const juce::String& trackName);  // generates UUID, returns it
    void createTrackWithId(const juce::String& id, const juce::String& trackName) override;
    void createAudioInputTrackWithId(const juce::String& id, const juce::String& name,
                                      int inputChannelStart, int inputChannelCount) override;
    void removeTrack(const juce::String& trackId) override;
    bool addTrackInstrument(const juce::String& trackId, const juce::String& pluginName,
                            LoadCallback onLoaded = nullptr) override;
    float getTrackPeakLevel(const juce::String& trackId) const;
    std::pair<float, float> getTrackPeakLevelStereo(const juce::String& trackId) const;
    void removeTrackInstrument(const juce::String& trackId) override;
    void setTrackMidiEnabled(const juce::String& trackId, bool enabled) override;
    void setTrackAudioEnabled(const juce::String& trackId, bool enabled) override;
    void setTrackInputMonitoring(const juce::String& trackId, bool enabled) override;
    void setTrackMuted(const juce::String& trackId, bool muted) override;
    void setTrackSoloed(const juce::String& trackId, bool soloed) override;
    void setTrackInputChannels(const juce::String& trackId, int start, int count) override;
    void setTrackGain(const juce::String& trackId, float gain) override;
    float getTrackGain(const juce::String& trackId) const;
    void renameTrack(const juce::String& trackId, const juce::String& newName) override;
    void setTrackOutputTarget(const juce::String& trackId, const juce::String& target) override;
    void clearAllTracks() override;

    // Bus management
    juce::String createBus(const juce::String& busName);  // generates UUID, returns it
    void createBusWithId(const juce::String& id, const juce::String& busName) override;
    void removeBus(const juce::String& busId) override;
    void renameBus(const juce::String& busId, const juce::String& newName) override;
    void setBusOutputTarget(const juce::String& busId, const juce::String& target) override;
    void setBusGain(const juce::String& busId, float gain) override;
    void setBusAudioEnabled(const juce::String& busId, bool enabled) override;

    struct EffectInfo { juce::String id; juce::String pluginName; };

    // Effects — unified for tracks and busses (parentId is track/bus UUID, or "Output" for master)
    bool addEffect(const juce::String& parentId, const juce::String& effectId,
                   const juce::String& pluginName, LoadCallback onLoaded = nullptr) override;
    void removeEffect(const juce::String& parentId, const juce::String& effectId) override;
    void clearAllBusses() override;

    // Master output
    void setMasterGain(float gain) override;
    void setMasterAudioEnabled(bool enabled) override;
    float getMasterGain() const;
    float getMasterPeakLevel() const;
    std::pair<float, float> getMasterPeakLevelStereo() const;
    std::vector<EffectInfo> getMasterEffects() const;

    // Sends
    void addSend(const juce::String& trackId, const juce::String& busId, float gain = 1.0f) override;
    void setSendGain(const juce::String& trackId, const juce::String& busId, float gain) override;

    // Access loaded processors by ID
    juce::AudioProcessor* getTrackInstrumentProcessor(const juce::String& trackId) const override;
    juce::AudioProcessor* getEffectProcessor(const juce::String& parentId,
                                              const juce::String& effectId) const override;

    // Query current state
    std::vector<juce::String> getTrackNames() const;
    std::vector<juce::String> getBusNames() const;
    juce::String getTrackPluginName(const juce::String& trackId) const override;
    bool isTrackMidiEnabled(const juce::String& trackId) const;
    std::vector<EffectInfo> getTrackEffects(const juce::String& trackId) const;
    std::vector<EffectInfo> getBusEffects(const juce::String& busId) const;
    std::vector<juce::String> getInputChannelNames() const override;
    std::vector<float> getInputPeakLevels() const;
    struct SendInfo { juce::String busName; float gain; float peakLevel; };
    std::vector<SendInfo> getTrackSends(const juce::String& trackId) const;
    float getBusGain(const juce::String& busId) const;
    float getBusPeakLevel(const juce::String& busId) const;
    std::pair<float, float> getBusPeakLevelStereo(const juce::String& busId) const;

    // Plugin editor windows
    struct PresetCallbacks {
        std::function<std::vector<juce::String>()> listPresets;
        std::function<void(const juce::String&)> savePreset;
        std::function<void(const juce::String&)> loadPreset;
        juce::String currentPresetName;
    };
    void openPluginEditor(const juce::String& parentId, const juce::String& effectName = "",
                          const juce::String& title = "",
                          PresetCallbacks presetCallbacks = {});
    void closeTopPluginEditor();

    // MIDI input to the graph
    void injectMidi(const juce::MidiMessage& message);

    // Sequencer playback — audio-thread-accurate scheduling
    void setPlaybackState(bool playing, double bpm);
    // Push the project's tempo events to the audio thread for
    // sample-accurate-at-buffer-boundary tempo switching.
    void setPlaybackTempoEvents(const std::vector<TempoEvent>& events);
    void setPlaybackBeatPosition(double beat);
    double getPlaybackBeatPosition() const;
    void setPlaybackLoop(bool enabled, double start, double end);
    void startPlayback(double beat, double bpm, bool loopOn, double loopStart, double loopEnd);
    void stopPlayback();
    void setArrangement(const Arrangement* arrangement);

    // Recording
    void setRecording(bool recording);
    RecordFIFO& getRecordFIFO();
    GraphWrapper::RecordDiag readRecordDiag() const;
    void resetRecordDiag();
    void setAudioRecordTargets(const std::vector<GraphWrapper::AudioRecordTarget>& targets);
    void clearAudioRecordTargets();
    void setMetronome(bool on, int beatsPerBar, float volume = -1.0f);
    double getCurrentSampleRate() const;
    void loadAudioFileForTrack(const juce::String& trackId, const juce::String& regionId,
                                const juce::String& filePath, double recordTempo, int fileSampleRate);

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::AudioProcessorGraph& getGraph() { return *graph; }
    juce::KnownPluginList& getKnownPlugins() { return knownPlugins; }
    GraphWrapper& getGraphWrapper() { return *graphWrapper; }

    // Temporarily detach the graph from the audio device so an external
    // caller (e.g. OfflineRenderer) has exclusive access to it. During
    // pause, the device callback outputs silence. Must be paired with
    // resumeDeviceProcessing() on the same thread.
    void pauseDeviceProcessing();
    void resumeDeviceProcessing();

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    bool pluginScanNeeded = false;

    std::unique_ptr<juce::AudioProcessorGraph> graph;
    std::unique_ptr<GraphWrapper> graphWrapper;
    std::unique_ptr<juce::AudioProcessorPlayer> player;

    // Graph node IDs
    juce::AudioProcessorGraph::NodeID midiInputNodeId;
    juce::AudioProcessorGraph::NodeID audioOutputNodeId;
    juce::AudioProcessorGraph::NodeID audioInputNodeId;
    juce::AudioProcessorGraph::Node::Ptr masterGainNode;  // GainProcessor

    // Shared effect node type
    struct EffectNode {
        juce::String id;          // registry effect UUID (stable identity)
        juce::String pluginName;  // display name of loaded plugin
        juce::AudioProcessorGraph::Node::Ptr node;
    };
    std::vector<EffectNode> masterEffects;

    // Track: one instrument + insert effects + sends + output gain
    struct Track {
        juce::String name;
        juce::String instrumentPluginName;
        juce::AudioProcessorGraph::Node::Ptr instrumentNode;
        juce::AudioProcessorGraph::Node::Ptr midiSourceNode;
        juce::AudioProcessorGraph::Node::Ptr audioFileNode;
        bool midiEnabled = true;
        bool audioEnabled = true;
        bool inputMonitoring = true;  // pass live audio input to output
        bool muted = false;
        bool soloed = false;
        juce::String outputTarget;  // "" = master, "none" = disconnected, UUID = bus
        TrackSourceType sourceType = TrackSourceType::Instrument;
        int inputChannelStart = -1;
        int inputChannelCount = 0;
        std::vector<EffectNode> effects;

        struct SendNode {
            juce::String busId;  // UUID key into busses map
            juce::AudioProcessorGraph::Node::Ptr gainNode;
        };
        std::vector<SendNode> sends;
        juce::AudioProcessorGraph::Node::Ptr outputGainNode;
    };
    std::map<juce::String, Track> tracks;  // keyed by UUID

    // Bus: effects chain + output gain
    struct Bus {
        juce::String name;
        bool audioEnabled = true;
        juce::String outputTarget;
        std::vector<EffectNode> effects;
        juce::AudioProcessorGraph::Node::Ptr outputGainNode;
    };
    bool masterAudioEnabled = true;
    std::map<juce::String, Bus> busses;  // keyed by UUID

    // Lookup helpers — resolve display name to UUID
    juce::String findTrackId(const juce::String& name) const;
    juce::String findBusId(const juce::String& name) const;
    static juce::String generateId();

    // Internal: shared effect manipulation
    bool addEffectToList(std::vector<EffectNode>& effects, const juce::String& parentId,
                         const juce::String& effectId, const juce::String& pluginName,
                         LoadCallback onLoaded);
    void removeEffectFromList(std::vector<EffectNode>& effects, const juce::String& parentId,
                              const juce::String& effectId);

    // Plugin editor windows
    std::vector<std::unique_ptr<juce::DocumentWindow>> editorWindows;

    // Index of unregistered .component bundles (metadata only, not loaded)
    struct ComponentInfo {
        juce::String path;
        juce::String name;
        juce::String factoryFunctionName;
        AudioComponentDescription desc;
        uint32_t version;
    };
    std::vector<ComponentInfo> componentIndex;

    // Input level metering (fixed max channels, atomics not moveable)
    static constexpr int maxInputChannels = 32;
    struct InputMeter : public juce::AudioIODeviceCallback {
        std::atomic<float> peaks[32] {};
        std::atomic<int> numChannels { 0 };
        void audioDeviceIOCallbackWithContext(const float* const* inputData, int numInputChannels,
                                              float* const* outputData, int numOutputChannels,
                                              int numSamples,
                                              const juce::AudioIODeviceCallbackContext&) override {
            // Zero output — JUCE sums all callback outputs, stale data causes feedback
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (outputData && outputData[ch])
                    juce::FloatVectorOperations::clear(outputData[ch], numSamples);
            int nc = std::min(numInputChannels, 32);
            numChannels.store(nc, std::memory_order_relaxed);
            for (int ch = 0; ch < nc; ++ch) {
                float peak = 0.0f;
                if (inputData && inputData[ch]) {
                    for (int i = 0; i < numSamples; ++i)
                        peak = std::max(peak, std::abs(inputData[ch][i]));
                }
                peaks[ch].store(peak, std::memory_order_relaxed);
            }
        }
        void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
            if (device)
                numChannels.store(device->getActiveInputChannels().countNumberOfSetBits(),
                                   std::memory_order_relaxed);
        }
        void audioDeviceStopped() override {}
    };
    InputMeter inputMeter;

    void setupGraph();
    void rebuildGraph();
    void updateMuteStates();
    void rebuildConnections();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    juce::PluginDescription findPluginDescription(const juce::String& pluginName);
};
