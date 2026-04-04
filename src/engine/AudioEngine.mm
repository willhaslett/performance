#include "engine/AudioEngine.h"
#include <AudioToolbox/AudioToolbox.h>

AudioEngine::AudioEngine()
    : graph(std::make_unique<juce::AudioProcessorGraph>()),
      player(std::make_unique<juce::AudioProcessorPlayer>()) {
    juce::addDefaultFormatsToManager(formatManager);
}

AudioEngine::~AudioEngine() {
    shutdown();
}

void AudioEngine::initialise() {
    auto result = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (result.isNotEmpty()) {
        DBG("Audio device error: " + result);
        return;
    }

    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        DBG("Audio device: " + device->getName());
        DBG("Sample rate: " + juce::String(device->getCurrentSampleRate()));
        DBG("Buffer size: " + juce::String(device->getCurrentBufferSizeSamples()));
    }

    setupGraph();
    scanForPlugins();
}

void AudioEngine::shutdown() {
    editorWindow.reset();
    deviceManager.removeAudioCallback(player.get());
    player->setProcessor(nullptr);
    graph->clear();
}

void AudioEngine::setupGraph() {
    auto* device = deviceManager.getCurrentAudioDevice();
    if (!device) return;

    graph->setPlayConfigDetails(
        0, 2,
        device->getCurrentSampleRate(),
        device->getCurrentBufferSizeSamples());
    graph->prepareToPlay(
        device->getCurrentSampleRate(),
        device->getCurrentBufferSizeSamples());

    // Create MIDI input node (we inject MIDI into this)
    auto midiInputNode = graph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
    midiInputNodeId = midiInputNode->nodeID;

    // Create audio output node
    auto audioOutputNode = graph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    audioOutputNodeId = audioOutputNode->nodeID;

    // Wire up the player
    player->setProcessor(graph.get());
    deviceManager.addAudioCallback(player.get());

    DBG("Audio graph ready");
}

void AudioEngine::scanForPlugins() {
    DBG("Scanning for AU plugins...");

    // First scan via system registry (gets Apple built-ins)
    for (auto* format : formatManager.getFormats()) {
        if (format->getName() != "AudioUnit")
            continue;

        juce::PluginDirectoryScanner scanner(
            knownPlugins,
            *format,
            format->getDefaultLocationsToSearch(),
            true,
            juce::File());

        juce::String name;
        while (scanner.scanNextFile(true, name)) {
            // scanning...
        }
    }

    // Also scan .component bundles directly (catches plugins not in system registry)
    scanComponentDirectory(juce::File("/Library/Audio/Plug-Ins/Components"));
    scanComponentDirectory(juce::File(juce::File::getSpecialLocation(
        juce::File::userHomeDirectory).getFullPathName() + "/Library/Audio/Plug-Ins/Components"));

    DBG("Found " + juce::String(knownPlugins.getNumTypes()) + " AU plugins");
}

void AudioEngine::scanComponentDirectory(const juce::File& directory) {
    if (!directory.isDirectory())
        return;

    juce::AudioPluginFormat* auFormat = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
        if (formatManager.getFormat(i)->getName() == "AudioUnit") {
            auFormat = formatManager.getFormat(i);
            break;
        }
    }
    if (!auFormat)
        return;

    DBG("Scanning directory: " + directory.getFullPathName());
    int found = 0;
    int registered = 0;

    for (const auto& entry : juce::RangedDirectoryIterator(directory, false, "*.component",
            juce::File::findDirectories)) {
        found++;
        auto componentFile = entry.getFile();
        auto componentPath = componentFile.getFullPathName();

        // Register the component bundle with AudioComponent system
        @autoreleasepool {
            NSString* nsPath = [NSString stringWithUTF8String:componentPath.toRawUTF8()];
            NSURL* bundleURL = [NSURL fileURLWithPath:nsPath isDirectory:YES];
            NSBundle* nsBundle = [NSBundle bundleWithURL:bundleURL];
            if (nsBundle && ![nsBundle isLoaded]) {
                [nsBundle load];
            }
        }

        // Try to register the component bundle with the AudioComponent system
        auto nsPath = componentPath.toRawUTF8();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault, (const UInt8*)nsPath, strlen(nsPath), true);

        if (url) {
            CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
            if (bundle) {
                // Read AudioComponents from Info.plist
                CFArrayRef audioComponents = (CFArrayRef)CFBundleGetValueForInfoDictionaryKey(
                    bundle, CFSTR("AudioComponents"));

                if (audioComponents && CFArrayGetCount(audioComponents) > 0) {
                    CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(audioComponents, 0);

                    auto getStr = [&](CFStringRef key) -> juce::String {
                        CFStringRef val = (CFStringRef)CFDictionaryGetValue(dict, key);
                        return val ? juce::String::fromCFString(val) : juce::String();
                    };

                    auto typeStr = getStr(CFSTR("type"));
                    auto subtypeStr = getStr(CFSTR("subtype"));
                    auto manuStr = getStr(CFSTR("manufacturer"));
                    auto nameStr = getStr(CFSTR("name"));

                    if (typeStr.isNotEmpty() && subtypeStr.isNotEmpty() && manuStr.isNotEmpty()) {
                        auto fourCharToOSType = [](const juce::String& s) -> OSType {
                            auto bytes = s.toRawUTF8();
                            return (OSType)((bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3]);
                        };

                        // Build the JUCE-style AU identifier string with category prefix
                        juce::String category;
                        if (typeStr == "aumu") category = "Synths/";
                        else if (typeStr == "aufx" || typeStr == "aumf") category = "Effects/";
                        else if (typeStr == "augn") category = "Generators/";
                        else if (typeStr == "aupn") category = "Panners/";
                        else if (typeStr == "aumx") category = "Mixers/";
                        else if (typeStr == "aumi") category = "MidiEffects/";

                        auto identifier = "AudioUnit:" + category + typeStr + "," + subtypeStr + "," + manuStr;

                        // Check if already known
                        bool alreadyKnown = false;
                        for (auto& type : knownPlugins.getTypes()) {
                            if (type.fileOrIdentifier == identifier) {
                                alreadyKnown = true;
                                break;
                            }
                        }

                        if (!alreadyKnown) {
                            // Create a PluginDescription manually
                            juce::PluginDescription desc;
                            desc.name = nameStr;
                            desc.pluginFormatName = "AudioUnit";
                            desc.fileOrIdentifier = identifier;
                            desc.manufacturerName = manuStr;
                            desc.isInstrument = (typeStr == "aumu");
                            desc.category = desc.isInstrument ? "Synth" : "Effect";
                            desc.numInputChannels = desc.isInstrument ? 0 : 2;
                            desc.numOutputChannels = 2;

                            // Read version
                            CFTypeRef versionVal = CFDictionaryGetValue(dict, CFSTR("version"));
                            if (versionVal && CFGetTypeID(versionVal) == CFNumberGetTypeID()) {
                                int ver = 0;
                                CFNumberGetValue((CFNumberRef)versionVal, kCFNumberIntType, &ver);
                                desc.version = juce::String(ver);
                            }

                            knownPlugins.addType(desc);
                            registered++;
                            DBG("  Registered: " + nameStr + " [" + identifier + "]");
                        }
                    }
                }

                CFRelease(bundle);
            }
            CFRelease(url);
        }
    }
    DBG("  Scanned " + juce::String(found) + " bundles, registered " + juce::String(registered) + " new plugins");
}

void AudioEngine::listAvailablePlugins() const {
    DBG("Available plugins:");
    for (auto& type : knownPlugins.getTypes()) {
        DBG("  [" + type.pluginFormatName + "] " + type.name +
            " (" + type.manufacturerName + ")");
    }
}

bool AudioEngine::loadInstrument(const juce::String& pluginName) {
    // Find the plugin description
    const juce::PluginDescription* desc = nullptr;
    for (auto& type : knownPlugins.getTypes()) {
        if (type.name.containsIgnoreCase(pluginName) && type.isInstrument) {
            desc = &type;
            break;
        }
    }

    if (!desc) {
        DBG("Plugin not found: " + pluginName);
        return false;
    }

    DBG("Loading: " + desc->name + " (" + desc->pluginFormatName + ")");

    juce::String errorMessage;
    auto instance = formatManager.createPluginInstance(
        *desc,
        graph->getSampleRate(),
        graph->getBlockSize(),
        errorMessage);

    if (!instance) {
        DBG("Failed to load plugin: " + errorMessage);
        return false;
    }

    // Remove old instrument if present
    if (instrumentNode) {
        graph->removeNode(instrumentNodeId);
        instrumentNode = nullptr;
    }

    // Add new instrument to graph
    instrumentNode = graph->addNode(std::move(instance));
    instrumentNodeId = instrumentNode->nodeID;

    connectInstrumentToOutput();

    DBG("Loaded: " + desc->name);
    return true;
}

void AudioEngine::connectInstrumentToOutput() {
    if (!instrumentNode) return;

    // Connect MIDI input -> instrument
    graph->addConnection({
        { midiInputNodeId, juce::AudioProcessorGraph::midiChannelIndex },
        { instrumentNodeId, juce::AudioProcessorGraph::midiChannelIndex }
    });

    // Connect instrument audio -> output (stereo)
    auto numOutputChannels = std::min(
        instrumentNode->getProcessor()->getTotalNumOutputChannels(), 2);

    for (int ch = 0; ch < numOutputChannels; ++ch) {
        graph->addConnection({
            { instrumentNodeId, ch },
            { audioOutputNodeId, ch }
        });
    }

    DBG("Instrument connected to output");
}

void AudioEngine::injectMidi(const juce::MidiMessage& message) {
    // Add MIDI message to the graph's MIDI input
    // This is called from the MIDI callback thread
    auto* midiNode = graph->getNodeForId(midiInputNodeId);
    if (midiNode) {
        // We pass MIDI through the player which feeds the graph
        player->getMidiMessageCollector().addMessageToQueue(message);
    }
}

void AudioEngine::openPluginEditor() {
    if (!instrumentNode) {
        DBG("No instrument loaded");
        return;
    }

    auto* processor = instrumentNode->getProcessor();
    if (!processor->hasEditor()) {
        DBG("Plugin has no editor");
        return;
    }

    auto* editor = processor->createEditor();
    if (!editor) return;

    editorWindow = std::make_unique<juce::DocumentWindow>(
        processor->getName(),
        juce::Colours::darkgrey,
        juce::DocumentWindow::closeButton);

    editorWindow->setContentOwned(editor, true);
    editorWindow->setUsingNativeTitleBar(true);
    editorWindow->centreWithSize(editor->getWidth(), editor->getHeight());
    editorWindow->setVisible(true);

    DBG("Opened editor for: " + processor->getName());
}
