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
    void createTrack(const juce::String& trackName);
    void removeTrack(const juce::String& trackName);
    bool addTrackInstrument(const juce::String& trackName, const juce::String& pluginName,
                            LoadCallback onLoaded = nullptr);
    float getTrackPeakLevel(const juce::String& trackName) const;
    void removeTrackInstrument(const juce::String& trackName);
    void setTrackMidiEnabled(const juce::String& trackName, bool enabled);
    void setTrackGain(const juce::String& trackName, float gain);
    float getTrackGain(const juce::String& trackName) const;
    void clearAllTracks();

    // Bus management
    void createBus(const juce::String& busName);
    void removeBus(const juce::String& busName);
    void setBusGain(const juce::String& busName, float gain);

    struct EffectInfo { juce::String name; juce::String pluginName; };

    // Effects — unified for tracks and busses
    bool addEffect(const juce::String& parentName, const juce::String& effectName,
                   const juce::String& pluginName, LoadCallback onLoaded = nullptr);
    void removeEffect(const juce::String& parentName, const juce::String& effectName);
    void clearAllBusses();

    // Master output
    void setMasterGain(float gain);
    float getMasterGain() const;
    float getMasterPeakLevel() const;
    std::vector<EffectInfo> getMasterEffects() const;

    // Sends
    void addSend(const juce::String& trackName, const juce::String& busName, float gain = 1.0f);
    void setSendGain(const juce::String& trackName, const juce::String& busName, float gain);

    // Access loaded processors by name
    juce::AudioProcessor* getTrackInstrumentProcessor(const juce::String& trackName) const;
    juce::AudioProcessor* getTrackEffectProcessor(const juce::String& trackName,
                                                   const juce::String& effectName) const;

    // Query current state
    std::vector<juce::String> getTrackNames() const;
    std::vector<juce::String> getBusNames() const;
    juce::String getTrackPluginName(const juce::String& trackName) const;
    bool isTrackMidiEnabled(const juce::String& trackName) const;
    std::vector<EffectInfo> getTrackEffects(const juce::String& trackName) const;
    std::vector<EffectInfo> getBusEffects(const juce::String& busName) const;
    struct SendInfo { juce::String busName; float gain; float peakLevel; };
    std::vector<SendInfo> getTrackSends(const juce::String& trackName) const;
    float getBusGain(const juce::String& busName) const;
    float getBusPeakLevel(const juce::String& busName) const;

    // Plugin editor windows
    void openPluginEditor(const juce::String& trackName, const juce::String& effectName = "");
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
        juce::String instrumentPluginName;
        juce::AudioProcessorGraph::Node::Ptr instrumentNode;
        bool midiEnabled = true;
        std::vector<EffectNode> effects;

        struct SendNode {
            juce::String busName;
            juce::AudioProcessorGraph::Node::Ptr gainNode;  // GainProcessor
        };
        std::vector<SendNode> sends;
        juce::AudioProcessorGraph::Node::Ptr outputGainNode;  // GainProcessor
    };
    std::map<juce::String, Track> tracks;

    // Bus: effects chain + output gain
    struct Bus {
        std::vector<EffectNode> effects;
        juce::AudioProcessorGraph::Node::Ptr outputGainNode;  // GainProcessor
    };
    std::map<juce::String, Bus> busses;

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
