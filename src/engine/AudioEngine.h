#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <AudioToolbox/AudioToolbox.h>
#include <map>

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    void initialise();
    void shutdown();

    // Plugin scanning
    void scanForPlugins();
    void scanComponentDirectory(const juce::File& directory);
    bool registerComponent(const juce::String& pluginName);
    void listAvailablePlugins() const;

    // Chain management — each chain is a named instrument + optional effects
    void createChain(const juce::String& chainName);
    bool addInstrument(const juce::String& chainName, const juce::String& pluginName);
    bool addEffect(const juce::String& chainName, const juce::String& effectName, const juce::String& pluginName);
    void removeChain(const juce::String& chainName);
    void clearAllChains();

    // Access loaded processors by name
    juce::AudioProcessor* getInstrumentProcessor(const juce::String& chainName) const;
    juce::AudioProcessor* getEffectProcessor(const juce::String& chainName, const juce::String& effectName) const;

    // Plugin editor windows
    void openPluginEditor(const juce::String& chainName, const juce::String& effectName = "");

    // Per-chain MIDI routing
    void setChainMidiEnabled(const juce::String& chainName, bool enabled);
    bool isChainMidiEnabled(const juce::String& chainName) const;

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

    // Named instrument chains
    struct InstrumentChain {
        juce::String instrumentPluginName;
        juce::AudioProcessorGraph::Node::Ptr instrumentNode;
        bool midiEnabled = true;
        struct EffectNode {
            juce::String name;
            juce::AudioProcessorGraph::Node::Ptr node;
        };
        std::vector<EffectNode> effects;
    };
    std::map<juce::String, InstrumentChain> chains;

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
    bool loadPluginCache();
    void savePluginCache();
};
