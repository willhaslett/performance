#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <AudioToolbox/AudioToolbox.h>

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    void initialise();
    void shutdown();

    // Plugin management
    void scanForPlugins();
    void scanComponentDirectory(const juce::File& directory);
    bool registerComponent(const juce::String& pluginName);
    void listAvailablePlugins() const;
    bool loadInstrument(const juce::String& pluginName);
    bool loadEffect(const juce::String& pluginName);
    void openPluginEditor(int index = 0);
    void clearChain();

    // Access loaded processors
    juce::AudioProcessor* getLoadedProcessor(int index = 0) const;
    int getLoadedProcessorCount() const { return (int)chainNodes.size(); }

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

    // Signal chain: instrument -> [effect, effect, ...] -> output
    struct ChainNode {
        juce::String name;
        juce::AudioProcessorGraph::Node::Ptr node;
    };
    std::vector<ChainNode> chainNodes;

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
    bool loadPlugin(const juce::String& pluginName, bool isInstrument);
    juce::PluginDescription findPluginDescription(const juce::String& pluginName);
};
