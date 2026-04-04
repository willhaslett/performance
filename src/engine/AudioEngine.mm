#include "engine/AudioEngine.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

// Keep loaded bundles alive for the lifetime of the process
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

void AudioEngine::scanForPlugins() {
    // Index third-party .component bundles (reads Info.plist only, no loading)
    scanComponentDirectory(juce::File("/Library/Audio/Plug-Ins/Components"));
    scanComponentDirectory(juce::File(juce::File::getSpecialLocation(
        juce::File::userHomeDirectory).getFullPathName() + "/Library/Audio/Plug-Ins/Components"));

    // Scan system-registered AUs via JUCE
    for (auto* format : formatManager.getFormats()) {
        if (format->getName() != "AudioUnit")
            continue;

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
    if (!directory.isDirectory())
        return;

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

            if (!typeStr || !subtypeStr || !manuStr || !factoryStr)
                continue;

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

            // Skip if already registered with AudioComponent system
            if (AudioComponentFindNext(nullptr, &info.desc) != nullptr)
                continue;

            componentIndex.push_back(info);
        }

        CFRelease(bundle);
    }
}

bool AudioEngine::registerComponent(const juce::String& pluginName) {
    for (auto& info : componentIndex) {
        if (!info.name.containsIgnoreCase(pluginName))
            continue;

        if (AudioComponentFindNext(nullptr, &info.desc) != nullptr)
            return true;

        auto pathCStr = info.path.toRawUTF8();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault, (const UInt8*)pathCStr, strlen(pathCStr), true);
        if (!url) continue;

        CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
        CFRelease(url);
        if (!bundle) continue;

        if (!CFBundleLoadExecutable(bundle)) {
            CFRelease(bundle);
            continue;
        }

        auto factoryCFStr = info.factoryFunctionName.toCFString();
        auto factoryFunc = (AudioComponentFactoryFunction)
            CFBundleGetFunctionPointerForName(bundle, factoryCFStr);
        CFRelease(factoryCFStr);

        if (!factoryFunc) {
            CFRelease(bundle);
            continue;
        }

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

static juce::String osTypeToString(OSType type) {
    char buf[5];
    buf[0] = (type >> 24) & 0xFF;
    buf[1] = (type >> 16) & 0xFF;
    buf[2] = (type >> 8) & 0xFF;
    buf[3] = type & 0xFF;
    buf[4] = 0;
    return juce::String(buf);
}

void AudioEngine::listAvailablePlugins() const {
    for (auto& type : knownPlugins.getTypes())
        DBG("  [system] " + type.name + " (" + type.manufacturerName + ")");
    for (auto& info : componentIndex)
        DBG("  [3p]     " + info.name);
}

bool AudioEngine::loadInstrument(const juce::String& pluginName) {
    juce::PluginDescription pluginDesc;
    bool found = false;

    // Search in system-registered plugins first
    for (auto& type : knownPlugins.getTypes()) {
        if (type.name.containsIgnoreCase(pluginName)) {
            pluginDesc = type;
            found = true;
            break;
        }
    }

    // If not found, try on-demand registration from component index
    if (!found) {
        if (registerComponent(pluginName)) {
            for (auto& info : componentIndex) {
                if (!info.name.containsIgnoreCase(pluginName))
                    continue;

                if (!AudioComponentFindNext(nullptr, &info.desc))
                    continue;

                juce::String category;
                if (info.desc.componentType == kAudioUnitType_MusicDevice) category = "Synths/";
                else if (info.desc.componentType == kAudioUnitType_Effect ||
                         info.desc.componentType == kAudioUnitType_MusicEffect) category = "Effects/";
                else if (info.desc.componentType == kAudioUnitType_Generator) category = "Generators/";

                pluginDesc.name = info.name;
                pluginDesc.pluginFormatName = "AudioUnit";
                pluginDesc.fileOrIdentifier = "AudioUnit:" + category +
                    osTypeToString(info.desc.componentType) + "," +
                    osTypeToString(info.desc.componentSubType) + "," +
                    osTypeToString(info.desc.componentManufacturer);
                pluginDesc.isInstrument = (info.desc.componentType == kAudioUnitType_MusicDevice);
                pluginDesc.category = pluginDesc.isInstrument ? "Synth" : "Effect";
                pluginDesc.numInputChannels = pluginDesc.isInstrument ? 0 : 2;
                pluginDesc.numOutputChannels = 2;

                found = true;
                break;
            }
        }
    }

    if (!found) {
        DBG("Plugin not found: " + pluginName);
        return false;
    }

    DBG("Loading: " + pluginDesc.name);

    formatManager.createPluginInstanceAsync(
        pluginDesc,
        graph->getSampleRate(),
        graph->getBlockSize(),
        [this, pluginDesc](std::unique_ptr<juce::AudioPluginInstance> instance,
                           const juce::String& errorMessage) {
            if (!instance) {
                DBG("Failed to load plugin: " + errorMessage);
                return;
            }

            if (instrumentNode) {
                graph->removeNode(instrumentNodeId);
                instrumentNode = nullptr;
            }

            instrumentNode = graph->addNode(std::move(instance));
            instrumentNodeId = instrumentNode->nodeID;
            connectInstrumentToOutput();
            DBG("Loaded: " + pluginDesc.name);
            openPluginEditor();
        });

    return true;
}

void AudioEngine::connectInstrumentToOutput() {
    if (!instrumentNode) return;

    graph->addConnection({
        { midiInputNodeId, juce::AudioProcessorGraph::midiChannelIndex },
        { instrumentNodeId, juce::AudioProcessorGraph::midiChannelIndex }
    });

    auto numOutputChannels = std::min(
        instrumentNode->getProcessor()->getTotalNumOutputChannels(), 2);

    for (int ch = 0; ch < numOutputChannels; ++ch) {
        graph->addConnection({
            { instrumentNodeId, ch },
            { audioOutputNodeId, ch }
        });
    }
}

void AudioEngine::injectMidi(const juce::MidiMessage& message) {
    auto* midiNode = graph->getNodeForId(midiInputNodeId);
    if (midiNode)
        player->getMidiMessageCollector().addMessageToQueue(message);
}

void AudioEngine::openPluginEditor() {
    if (!instrumentNode) return;

    auto* processor = instrumentNode->getProcessor();
    if (!processor->hasEditor()) return;

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
}
