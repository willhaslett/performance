#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <AudioToolbox/AudioToolbox.h>
#include <functional>
#include <map>

class GainProcessor;

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    void initialise();
    void shutdown();

    // Plugin scanning + cache
    void scanForPlugins();
    void scanComponentDirectory(const juce::File& directory);
    bool registerComponent(const juce::String& pluginName);
    void listAvailablePlugins() const;
    bool loadPluginCache();
    void savePluginCache();

    // Track management
    using LoadCallback = std::function<void()>;
    juce::String createTrack(const juce::String& trackName);  // generates UUID, returns it
    void createTrackWithId(const juce::String& id, const juce::String& trackName);  // use given UUID
    void removeTrack(const juce::String& trackId);
    bool addTrackInstrument(const juce::String& trackId, const juce::String& pluginName,
                            LoadCallback onLoaded = nullptr);
    float getTrackPeakLevel(const juce::String& trackId) const;
    void removeTrackInstrument(const juce::String& trackId);
    void setTrackMidiEnabled(const juce::String& trackId, bool enabled);
    void setTrackGain(const juce::String& trackId, float gain);
    float getTrackGain(const juce::String& trackId) const;
    void renameTrack(const juce::String& trackId, const juce::String& newName);
    void clearAllTracks();

    // Bus management
    juce::String createBus(const juce::String& busName);  // generates UUID, returns it
    void createBusWithId(const juce::String& id, const juce::String& busName);  // use given UUID
    void removeBus(const juce::String& busId);
    void renameBus(const juce::String& busId, const juce::String& newName);
    void setBusGain(const juce::String& busId, float gain);

    struct EffectInfo { juce::String name; juce::String pluginName; };

    // Effects — unified for tracks and busses (parentId is track/bus UUID, or "Output" for master)
    bool addEffect(const juce::String& parentId, const juce::String& effectName,
                   const juce::String& pluginName, LoadCallback onLoaded = nullptr);
    void removeEffect(const juce::String& parentId, const juce::String& effectName);
    void clearAllBusses();

    // Master output
    void setMasterGain(float gain);
    float getMasterGain() const;
    float getMasterPeakLevel() const;
    std::vector<EffectInfo> getMasterEffects() const;

    // Sends
    void addSend(const juce::String& trackId, const juce::String& busId, float gain = 1.0f);
    void setSendGain(const juce::String& trackId, const juce::String& busId, float gain);

    // Access loaded processors by ID
    juce::AudioProcessor* getTrackInstrumentProcessor(const juce::String& trackId) const;
    juce::AudioProcessor* getTrackEffectProcessor(const juce::String& trackId,
                                                   const juce::String& effectName) const;

    // Query current state
    std::vector<juce::String> getTrackNames() const;
    std::vector<juce::String> getBusNames() const;
    juce::String getTrackPluginName(const juce::String& trackId) const;
    bool isTrackMidiEnabled(const juce::String& trackId) const;
    std::vector<EffectInfo> getTrackEffects(const juce::String& trackId) const;
    std::vector<EffectInfo> getBusEffects(const juce::String& busId) const;
    struct SendInfo { juce::String busName; float gain; float peakLevel; };
    std::vector<SendInfo> getTrackSends(const juce::String& trackId) const;
    float getBusGain(const juce::String& busId) const;
    float getBusPeakLevel(const juce::String& busId) const;

    // Plugin editor windows
    struct PresetCallbacks {
        std::function<std::vector<juce::String>()> listPresets;
        std::function<void(const juce::String&)> savePreset;
        std::function<void(const juce::String&)> loadPreset;
        juce::String currentPresetName;
    };
    void openPluginEditor(const juce::String& parentId, const juce::String& effectName = "",
                          PresetCallbacks presetCallbacks = {});
    void closeTopPluginEditor();

    // MIDI input to the graph
    void injectMidi(const juce::MidiMessage& message);

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::AudioProcessorGraph& getGraph() { return *graph; }
    juce::KnownPluginList& getKnownPlugins() { return knownPlugins; }

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    std::unique_ptr<juce::AudioProcessorGraph> graph;
    std::unique_ptr<juce::AudioProcessorPlayer> player;

    // Graph node IDs
    juce::AudioProcessorGraph::NodeID midiInputNodeId;
    juce::AudioProcessorGraph::NodeID audioOutputNodeId;
    juce::AudioProcessorGraph::Node::Ptr masterGainNode;  // GainProcessor

    // Shared effect node type
    struct EffectNode {
        juce::String name;
        juce::AudioProcessorGraph::Node::Ptr node;
    };
    std::vector<EffectNode> masterEffects;

    // Track: one instrument + insert effects + sends + output gain
    struct Track {
        juce::String name;
        juce::String instrumentPluginName;
        juce::AudioProcessorGraph::Node::Ptr instrumentNode;
        bool midiEnabled = true;
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
        std::vector<EffectNode> effects;
        juce::AudioProcessorGraph::Node::Ptr outputGainNode;
    };
    std::map<juce::String, Bus> busses;  // keyed by UUID

    // Lookup helpers — resolve display name to UUID
    juce::String findTrackId(const juce::String& name) const;
    juce::String findBusId(const juce::String& name) const;
    static juce::String generateId();

    // Internal: shared effect manipulation
    bool addEffectToList(std::vector<EffectNode>& effects, const juce::String& parentName,
                         const juce::String& effectName, const juce::String& pluginName,
                         LoadCallback onLoaded);
    void removeEffectFromList(std::vector<EffectNode>& effects, const juce::String& parentName,
                              const juce::String& effectName);

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

    void setupGraph();
    void rebuildConnections();
    juce::PluginDescription findPluginDescription(const juce::String& pluginName);
};
