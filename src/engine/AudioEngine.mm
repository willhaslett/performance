#include "engine/AudioEngine.h"
#include "engine/Log.h"
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
        perfLog("[Engine] Audio device error: %s\n", result.toRawUTF8());
        return;
    }

    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        perfLog("[Engine] Audio device: %s\n", device->getName().toRawUTF8());
        perfLog("[Engine]   Sample rate: %.0f\n", device->getCurrentSampleRate());
        perfLog("[Engine]   Buffer size: %d\n", device->getCurrentBufferSizeSamples());
    }

    setupGraph();

    if (!loadPluginCache())
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

static juce::File getPluginCacheFile() {
    auto configDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                         .getChildFile(".config/performance");
    configDir.createDirectory();
    return configDir.getChildFile("plugin-cache.xml");
}

void AudioEngine::scanForPlugins() {
    perfLog("[Engine] Scanning for plugins...\n");

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

    perfLog("[Engine] Scan complete: %d system, %d third-party indexed\n",
            knownPlugins.getNumTypes(), (int)componentIndex.size());

    savePluginCache();
}

bool AudioEngine::loadPluginCache() {
    auto cacheFile = getPluginCacheFile();
    if (!cacheFile.existsAsFile()) return false;

    auto xml = juce::parseXML(cacheFile);
    if (!xml) return false;

    // Load KnownPluginList
    auto* knownXml = xml->getChildByName("KnownPlugins");
    if (knownXml)
        knownPlugins.recreateFromXml(*knownXml);

    // Load component index
    componentIndex.clear();
    auto* compXml = xml->getChildByName("ComponentIndex");
    if (compXml) {
        for (auto* entry : compXml->getChildIterator()) {
            ComponentInfo info;
            info.path = entry->getStringAttribute("path");
            info.name = entry->getStringAttribute("name");
            info.factoryFunctionName = entry->getStringAttribute("factory");
            info.desc.componentType = (OSType)entry->getIntAttribute("type");
            info.desc.componentSubType = (OSType)entry->getIntAttribute("subtype");
            info.desc.componentManufacturer = (OSType)entry->getIntAttribute("manufacturer");
            info.desc.componentFlags = 0;
            info.desc.componentFlagsMask = 0;
            info.version = (uint32_t)entry->getIntAttribute("version");
            componentIndex.push_back(info);
        }
    }

    perfLog("[Engine] Loaded plugin cache: %d system, %d third-party\n",
            knownPlugins.getNumTypes(), (int)componentIndex.size());
    return true;
}

void AudioEngine::savePluginCache() {
    juce::XmlElement root("PluginCache");

    // Save KnownPluginList
    if (auto knownXml = knownPlugins.createXml())
        root.addChildElement(knownXml.release());

    // Save component index
    auto* compXml = root.createNewChildElement("ComponentIndex");
    for (auto& info : componentIndex) {
        auto* entry = compXml->createNewChildElement("Component");
        entry->setAttribute("path", info.path);
        entry->setAttribute("name", info.name);
        entry->setAttribute("factory", info.factoryFunctionName);
        entry->setAttribute("type", (int)info.desc.componentType);
        entry->setAttribute("subtype", (int)info.desc.componentSubType);
        entry->setAttribute("manufacturer", (int)info.desc.componentManufacturer);
        entry->setAttribute("version", (int)info.version);
    }

    auto cacheFile = getPluginCacheFile();
    root.writeTo(cacheFile);
    perfLog("[Engine] Saved plugin cache to %s\n", cacheFile.getFullPathName().toRawUTF8());
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
        perfLog("  [system] %s (%s)\n", type.name.toRawUTF8(), type.manufacturerName.toRawUTF8());
    for (auto& info : componentIndex)
        perfLog("  [3p]     %s\n", info.name.toRawUTF8());
}

// --- Chain management ---

void AudioEngine::createChain(const juce::String& chainName) {
    chains[chainName] = {};
    perfLog("[Engine] Created chain: %s\n", chainName.toRawUTF8());
}

bool AudioEngine::addInstrument(const juce::String& chainName, const juce::String& pluginName) {
    auto it = chains.find(chainName);
    if (it == chains.end()) {
        perfLog("[Engine] Chain not found: %s\n", chainName.toRawUTF8());
        return false;
    }

    auto desc = findPluginDescription(pluginName);
    if (desc.name.isEmpty()) {
        perfLog("[Engine] Plugin not found: %s\n", pluginName.toRawUTF8());
        return false;
    }

    it->second.instrumentPluginName = pluginName;
    perfLog("[Engine] Loading instrument: %s -> chain \"%s\"\n",
            desc.name.toRawUTF8(), chainName.toRawUTF8());

    formatManager.createPluginInstanceAsync(
        desc, graph->getSampleRate(), graph->getBlockSize(),
        [this, chainName, desc](std::unique_ptr<juce::AudioPluginInstance> instance,
                                 const juce::String& error) {
            if (!instance) {
                perfLog("[Engine] FAILED to load: %s\n", error.toRawUTF8());
                return;
            }
            auto it = chains.find(chainName);
            if (it == chains.end()) return;
            it->second.instrumentNode = graph->addNode(std::move(instance));
            rebuildConnections();
            perfLog("[Engine] Loaded instrument: %s in chain \"%s\"\n",
                    desc.name.toRawUTF8(), chainName.toRawUTF8());
        });

    return true;
}

bool AudioEngine::addEffect(const juce::String& chainName, const juce::String& effectName,
                              const juce::String& pluginName) {
    auto it = chains.find(chainName);
    if (it == chains.end()) {
        perfLog("[Engine] Chain not found: %s\n", chainName.toRawUTF8());
        return false;
    }

    auto desc = findPluginDescription(pluginName);
    if (desc.name.isEmpty()) {
        perfLog("[Engine] Plugin not found: %s\n", pluginName.toRawUTF8());
        return false;
    }

    it->second.effects.push_back({ effectName, nullptr });
    auto effectIndex = it->second.effects.size() - 1;
    perfLog("[Engine] Loading effect: %s as \"%s\" -> chain \"%s\"\n",
            desc.name.toRawUTF8(), effectName.toRawUTF8(), chainName.toRawUTF8());

    formatManager.createPluginInstanceAsync(
        desc, graph->getSampleRate(), graph->getBlockSize(),
        [this, chainName, effectIndex, desc](std::unique_ptr<juce::AudioPluginInstance> instance,
                                              const juce::String& error) {
            if (!instance) {
                perfLog("[Engine] FAILED to load: %s\n", error.toRawUTF8());
                return;
            }
            auto it = chains.find(chainName);
            if (it == chains.end()) return;
            if (effectIndex >= it->second.effects.size()) return;
            it->second.effects[effectIndex].node = graph->addNode(std::move(instance));
            rebuildConnections();
            perfLog("[Engine] Loaded effect: %s as \"%s\" in chain \"%s\"\n",
                    desc.name.toRawUTF8(), it->second.effects[effectIndex].name.toRawUTF8(),
                    chainName.toRawUTF8());
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

        auto* proc = chain.instrumentNode->getProcessor();
        int numOut = proc->getTotalNumOutputChannels();
        int numIn = proc->getTotalNumInputChannels();
        perfLog("[Engine] Wiring chain \"%s\": %s (%d in, %d out, midi=%s)\n",
                chainName.toRawUTF8(), proc->getName().toRawUTF8(),
                numIn, numOut, chain.midiEnabled ? "on" : "off");

        // MIDI input -> instrument (only if MIDI enabled)
        if (chain.midiEnabled) {
            graph->addConnection({
                { midiInputNodeId, juce::AudioProcessorGraph::midiChannelIndex },
                { chain.instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex }
            });
        }

        // Build the audio path: instrument -> effects -> output
        auto prevNodeId = chain.instrumentNode->nodeID;
        int prevNumOut = std::min(numOut, 2);

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

void AudioEngine::setChainMidiEnabled(const juce::String& chainName, bool enabled) {
    auto it = chains.find(chainName);
    if (it == chains.end()) return;
    if (it->second.midiEnabled == enabled) return;
    it->second.midiEnabled = enabled;
    rebuildConnections();
    perfLog("[Engine] MIDI %s for chain \"%s\"\n",
            enabled ? "enabled" : "disabled", chainName.toRawUTF8());
}

bool AudioEngine::isChainMidiEnabled(const juce::String& chainName) const {
    auto it = chains.find(chainName);
    return it != chains.end() && it->second.midiEnabled;
}

void AudioEngine::injectMidi(const juce::MidiMessage& message) {
    player->getMidiMessageCollector().addMessageToQueue(message);
}
