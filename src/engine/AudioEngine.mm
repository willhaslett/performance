#include "engine/AudioEngine.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

static std::vector<CFBundleRef> loadedBundles;

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
        DBG("  Sample rate: " + juce::String(device->getCurrentSampleRate()));
        DBG("  Buffer size: " + juce::String(device->getCurrentBufferSizeSamples()));
    }

    setupGraph();
    scanForPlugins();
}

void AudioEngine::shutdown() {
    editorWindows.clear();
    deviceManager.removeAudioCallback(player.get());
    player->setProcessor(nullptr);
    graph->clear();
    chains.clear();
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

    auto midiInputNode = graph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
    midiInputNodeId = midiInputNode->nodeID;

    auto audioOutputNode = graph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    audioOutputNodeId = audioOutputNode->nodeID;

    player->setProcessor(graph.get());
    deviceManager.addAudioCallback(player.get());
}

// --- Plugin scanning ---

void AudioEngine::scanForPlugins() {
    scanComponentDirectory(juce::File("/Library/Audio/Plug-Ins/Components"));
    scanComponentDirectory(juce::File(juce::File::getSpecialLocation(
        juce::File::userHomeDirectory).getFullPathName() + "/Library/Audio/Plug-Ins/Components"));

    for (auto* format : formatManager.getFormats()) {
        if (format->getName() != "AudioUnit") continue;
        juce::PluginDirectoryScanner scanner(
            knownPlugins, *format,
            format->getDefaultLocationsToSearch(), true, juce::File());
        juce::String name;
        while (scanner.scanNextFile(true, name)) {}
    }

    DBG("Plugins: " + juce::String(knownPlugins.getNumTypes()) + " system, " +
        juce::String(componentIndex.size()) + " third-party indexed");
}

void AudioEngine::scanComponentDirectory(const juce::File& directory) {
    if (!directory.isDirectory()) return;

    for (const auto& entry : juce::RangedDirectoryIterator(directory, false, "*.component",
            juce::File::findDirectories)) {
        auto componentPath = entry.getFile().getFullPathName();
        auto pathCStr = componentPath.toRawUTF8();

        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault, (const UInt8*)pathCStr, strlen(pathCStr), true);
        if (!url) continue;

        CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
        CFRelease(url);
        if (!bundle) continue;

        CFArrayRef audioComponents = (CFArrayRef)CFBundleGetValueForInfoDictionaryKey(
            bundle, CFSTR("AudioComponents"));
        if (!audioComponents || CFArrayGetCount(audioComponents) == 0) {
            CFRelease(bundle);
            continue;
        }

        for (CFIndex i = 0; i < CFArrayGetCount(audioComponents); ++i) {
            CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(audioComponents, i);
            auto getCFStr = [&](CFStringRef key) -> CFStringRef {
                return (CFStringRef)CFDictionaryGetValue(dict, key);
            };

            CFStringRef typeStr = getCFStr(CFSTR("type"));
            CFStringRef subtypeStr = getCFStr(CFSTR("subtype"));
            CFStringRef manuStr = getCFStr(CFSTR("manufacturer"));
            CFStringRef nameStr = getCFStr(CFSTR("name"));
            CFStringRef factoryStr = getCFStr(CFSTR("factoryFunction"));
            if (!typeStr || !subtypeStr || !manuStr || !factoryStr) continue;

            auto cfStrToOSType = [](CFStringRef str) -> OSType {
                char buf[5] = {0};
                CFStringGetCString(str, buf, 5, kCFStringEncodingASCII);
                return (OSType)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
            };

            ComponentInfo info;
            info.path = componentPath;
            info.name = nameStr ? juce::String::fromCFString(nameStr)
                                : entry.getFile().getFileNameWithoutExtension();
            info.factoryFunctionName = juce::String::fromCFString(factoryStr);
            info.desc.componentType = cfStrToOSType(typeStr);
            info.desc.componentSubType = cfStrToOSType(subtypeStr);
            info.desc.componentManufacturer = cfStrToOSType(manuStr);
            info.desc.componentFlags = 0;
            info.desc.componentFlagsMask = 0;
            info.version = 0;

            CFTypeRef versionVal = CFDictionaryGetValue(dict, CFSTR("version"));
            if (versionVal && CFGetTypeID(versionVal) == CFNumberGetTypeID())
                CFNumberGetValue((CFNumberRef)versionVal, kCFNumberIntType, (int*)&info.version);

            if (AudioComponentFindNext(nullptr, &info.desc) != nullptr) continue;
            componentIndex.push_back(info);
        }
        CFRelease(bundle);
    }
}

bool AudioEngine::registerComponent(const juce::String& pluginName) {
    for (auto& info : componentIndex) {
        if (!info.name.containsIgnoreCase(pluginName)) continue;
        if (AudioComponentFindNext(nullptr, &info.desc) != nullptr) return true;

        auto pathCStr = info.path.toRawUTF8();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault, (const UInt8*)pathCStr, strlen(pathCStr), true);
        if (!url) continue;

        CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
        CFRelease(url);
        if (!bundle) continue;

        if (!CFBundleLoadExecutable(bundle)) { CFRelease(bundle); continue; }

        auto factoryCFStr = info.factoryFunctionName.toCFString();
        auto factoryFunc = (AudioComponentFactoryFunction)
            CFBundleGetFunctionPointerForName(bundle, factoryCFStr);
        CFRelease(factoryCFStr);
        if (!factoryFunc) { CFRelease(bundle); continue; }

        auto nameCFStr = info.name.toCFString();
        AudioComponent comp = AudioComponentRegister(&info.desc, nameCFStr, info.version, factoryFunc);
        CFRelease(nameCFStr);

        if (comp) {
            loadedBundles.push_back(bundle);
            return true;
        }
        CFRelease(bundle);
    }
    return false;
}

// --- Plugin description lookup ---

static juce::String osTypeToString(OSType type) {
    char buf[5];
    buf[0] = (type >> 24) & 0xFF;
    buf[1] = (type >> 16) & 0xFF;
    buf[2] = (type >> 8) & 0xFF;
    buf[3] = type & 0xFF;
    buf[4] = 0;
    return juce::String(buf);
}

juce::PluginDescription AudioEngine::findPluginDescription(const juce::String& pluginName) {
    // Check system-registered plugins
    for (auto& type : knownPlugins.getTypes()) {
        if (type.name.containsIgnoreCase(pluginName))
            return type;
    }

    // Try on-demand registration from component index
    if (registerComponent(pluginName)) {
        for (auto& info : componentIndex) {
            if (!info.name.containsIgnoreCase(pluginName)) continue;
            if (!AudioComponentFindNext(nullptr, &info.desc)) continue;

            juce::String category;
            if (info.desc.componentType == kAudioUnitType_MusicDevice) category = "Synths/";
            else if (info.desc.componentType == kAudioUnitType_Effect ||
                     info.desc.componentType == kAudioUnitType_MusicEffect) category = "Effects/";
            else if (info.desc.componentType == kAudioUnitType_Generator) category = "Generators/";

            juce::PluginDescription pd;
            pd.name = info.name;
            pd.pluginFormatName = "AudioUnit";
            pd.fileOrIdentifier = "AudioUnit:" + category +
                osTypeToString(info.desc.componentType) + "," +
                osTypeToString(info.desc.componentSubType) + "," +
                osTypeToString(info.desc.componentManufacturer);
            pd.isInstrument = (info.desc.componentType == kAudioUnitType_MusicDevice);
            pd.category = pd.isInstrument ? "Synth" : "Effect";
            pd.numInputChannels = pd.isInstrument ? 0 : 2;
            pd.numOutputChannels = 2;
            return pd;
        }
    }

    return {}; // empty = not found
}

void AudioEngine::listAvailablePlugins() const {
    for (auto& type : knownPlugins.getTypes())
        DBG("  [system] " + type.name + " (" + type.manufacturerName + ")");
    for (auto& info : componentIndex)
        DBG("  [3p]     " + info.name);
}

// --- Chain management ---

void AudioEngine::createChain(const juce::String& chainName) {
    chains[chainName] = {};
    DBG("Created chain: " + chainName);
}

bool AudioEngine::addInstrument(const juce::String& chainName, const juce::String& pluginName) {
    auto it = chains.find(chainName);
    if (it == chains.end()) {
        DBG("Chain not found: " + chainName);
        return false;
    }

    auto desc = findPluginDescription(pluginName);
    if (desc.name.isEmpty()) {
        DBG("Plugin not found: " + pluginName);
        return false;
    }

    it->second.instrumentPluginName = pluginName;
    DBG("Loading instrument: " + desc.name + " -> chain \"" + chainName + "\"");

    formatManager.createPluginInstanceAsync(
        desc, graph->getSampleRate(), graph->getBlockSize(),
        [this, chainName, desc](std::unique_ptr<juce::AudioPluginInstance> instance,
                                 const juce::String& error) {
            if (!instance) { DBG("Failed to load: " + error); return; }
            auto it = chains.find(chainName);
            if (it == chains.end()) return;
            it->second.instrumentNode = graph->addNode(std::move(instance));
            rebuildConnections();
            DBG("Loaded instrument: " + desc.name + " in chain \"" + chainName + "\"");
        });

    return true;
}

bool AudioEngine::addEffect(const juce::String& chainName, const juce::String& effectName,
                              const juce::String& pluginName) {
    auto it = chains.find(chainName);
    if (it == chains.end()) {
        DBG("Chain not found: " + chainName);
        return false;
    }

    auto desc = findPluginDescription(pluginName);
    if (desc.name.isEmpty()) {
        DBG("Plugin not found: " + pluginName);
        return false;
    }

    it->second.effects.push_back({ effectName, nullptr });
    auto effectIndex = it->second.effects.size() - 1;
    DBG("Loading effect: " + desc.name + " as \"" + effectName + "\" -> chain \"" + chainName + "\"");

    formatManager.createPluginInstanceAsync(
        desc, graph->getSampleRate(), graph->getBlockSize(),
        [this, chainName, effectIndex, desc](std::unique_ptr<juce::AudioPluginInstance> instance,
                                              const juce::String& error) {
            if (!instance) { DBG("Failed to load: " + error); return; }
            auto it = chains.find(chainName);
            if (it == chains.end()) return;
            if (effectIndex >= it->second.effects.size()) return;
            it->second.effects[effectIndex].node = graph->addNode(std::move(instance));
            rebuildConnections();
            DBG("Loaded effect: " + desc.name + " as \"" +
                it->second.effects[effectIndex].name + "\" in chain \"" + chainName + "\"");
        });

    return true;
}

void AudioEngine::removeChain(const juce::String& chainName) {
    auto it = chains.find(chainName);
    if (it == chains.end()) return;

    if (it->second.instrumentNode)
        graph->removeNode(it->second.instrumentNode->nodeID);
    for (auto& fx : it->second.effects) {
        if (fx.node)
            graph->removeNode(fx.node->nodeID);
    }

    chains.erase(it);
    rebuildConnections();
    DBG("Removed chain: " + chainName);
}

void AudioEngine::clearAllChains() {
    editorWindows.clear();
    for (auto& [name, chain] : chains) {
        if (chain.instrumentNode)
            graph->removeNode(chain.instrumentNode->nodeID);
        for (auto& fx : chain.effects) {
            if (fx.node)
                graph->removeNode(fx.node->nodeID);
        }
    }
    chains.clear();
    rebuildConnections();
}

void AudioEngine::rebuildConnections() {
    for (auto& conn : graph->getConnections())
        graph->removeConnection(conn);

    for (auto& [chainName, chain] : chains) {
        if (!chain.instrumentNode) continue;

        // MIDI input -> instrument
        graph->addConnection({
            { midiInputNodeId, juce::AudioProcessorGraph::midiChannelIndex },
            { chain.instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex }
        });

        // Build the audio path: instrument -> effects -> output
        auto prevNodeId = chain.instrumentNode->nodeID;
        int prevNumOut = std::min(
            chain.instrumentNode->getProcessor()->getTotalNumOutputChannels(), 2);

        for (auto& fx : chain.effects) {
            if (!fx.node) continue;
            for (int ch = 0; ch < prevNumOut; ++ch)
                graph->addConnection({ { prevNodeId, ch }, { fx.node->nodeID, ch } });
            prevNodeId = fx.node->nodeID;
            prevNumOut = std::min(fx.node->getProcessor()->getTotalNumOutputChannels(), 2);
        }

        // Last node -> audio output
        for (int ch = 0; ch < prevNumOut; ++ch)
            graph->addConnection({ { prevNodeId, ch }, { audioOutputNodeId, ch } });
    }
}

juce::AudioProcessor* AudioEngine::getInstrumentProcessor(const juce::String& chainName) const {
    auto it = chains.find(chainName);
    if (it != chains.end() && it->second.instrumentNode)
        return it->second.instrumentNode->getProcessor();
    return nullptr;
}

juce::AudioProcessor* AudioEngine::getEffectProcessor(const juce::String& chainName,
                                                        const juce::String& effectName) const {
    auto it = chains.find(chainName);
    if (it == chains.end()) return nullptr;
    for (auto& fx : it->second.effects) {
        if (fx.name == effectName && fx.node)
            return fx.node->getProcessor();
    }
    return nullptr;
}

void AudioEngine::openPluginEditor(const juce::String& chainName, const juce::String& effectName) {
    juce::AudioProcessor* processor = nullptr;
    if (effectName.isEmpty())
        processor = getInstrumentProcessor(chainName);
    else
        processor = getEffectProcessor(chainName, effectName);

    if (!processor || !processor->hasEditor()) return;

    auto* editor = processor->createEditor();
    if (!editor) return;

    auto window = std::make_unique<juce::DocumentWindow>(
        processor->getName(),
        juce::Colours::darkgrey,
        juce::DocumentWindow::closeButton);

    window->setContentOwned(editor, true);
    window->setUsingNativeTitleBar(true);
    window->centreWithSize(editor->getWidth(), editor->getHeight());
    window->setVisible(true);

    editorWindows.push_back(std::move(window));
}

void AudioEngine::injectMidi(const juce::MidiMessage& message) {
    player->getMidiMessageCollector().addMessageToQueue(message);
}
