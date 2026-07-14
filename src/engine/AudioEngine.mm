#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/Log.h"
#include "gui/SaveAsDialog.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

static std::vector<CFBundleRef> loadedBundles;

// Probe each contributing CoreAudio property for every input device on
// the system, so we can see which property is dominating the reported
// latency. JUCE's getInputLatencyInSamples() returns the sum of:
//   deviceLatency + safetyOffset + framesInBuffer + streamLatency
// We dump them individually so the breakdown is visible. The fix path
// depends on which one is bloated — safety offset is usually the most
// addressable (driver convention, sometimes overridable), device
// latency is usually fixed hardware/driver constant, stream latency
// hints at format-conversion overhead.
static void dumpInputDeviceLatencyBreakdown() {
    AudioObjectPropertyAddress addrDevices = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addrDevices, 0, nullptr, &dataSize) != noErr) return;
    std::vector<AudioObjectID> ids(dataSize / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addrDevices, 0, nullptr, &dataSize, ids.data()) != noErr) return;

    for (auto devId : ids) {
        // Device must have input streams to be considered an input device.
        AudioObjectPropertyAddress addrStreams = {
            kAudioDevicePropertyStreams,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };
        UInt32 streamsSize = 0;
        if (AudioObjectGetPropertyDataSize(devId, &addrStreams, 0, nullptr, &streamsSize) != noErr || streamsSize == 0)
            continue;
        std::vector<AudioStreamID> streams(streamsSize / sizeof(AudioStreamID));
        AudioObjectGetPropertyData(devId, &addrStreams, 0, nullptr, &streamsSize, streams.data());

        // Device name.
        CFStringRef nameRef = nullptr;
        UInt32 nameSize = sizeof(nameRef);
        AudioObjectPropertyAddress addrName = {
            kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
        };
        AudioObjectGetPropertyData(devId, &addrName, 0, nullptr, &nameSize, &nameRef);
        char nameBuf[256] = "";
        if (nameRef) {
            CFStringGetCString(nameRef, nameBuf, sizeof(nameBuf), kCFStringEncodingUTF8);
            CFRelease(nameRef);
        }

        auto getU32 = [](AudioObjectID obj, AudioObjectPropertySelector sel,
                          AudioObjectPropertyScope scope) -> UInt32 {
            AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMain };
            UInt32 v = 0;
            UInt32 sz = sizeof(v);
            if (AudioObjectGetPropertyData(obj, &a, 0, nullptr, &sz, &v) != noErr) return 0;
            return v;
        };

        UInt32 deviceLat   = getU32(devId,         kAudioDevicePropertyLatency,        kAudioDevicePropertyScopeInput);
        UInt32 safetyOff   = getU32(devId,         kAudioDevicePropertySafetyOffset,   kAudioDevicePropertyScopeInput);
        UInt32 bufFrames   = getU32(devId,         kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeWildcard);
        UInt32 streamLat   = getU32(streams.front(), kAudioStreamPropertyLatency,      kAudioDevicePropertyScopeInput);
        UInt32 total       = deviceLat + safetyOff + bufFrames + streamLat;

        perfLog("[Engine][lat:probe] '%s': device=%u + safety=%u + buf=%u + stream=%u = %u samples (input)\n",
                nameBuf, deviceLat, safetyOff, bufFrames, streamLat, total);

        // For each stream on this input device, dump its individual
        // latency + terminal type. If there are multiple streams and one
        // has lower latency than streams.front(), JUCE/AUHAL is picking
        // a sub-optimal one and we may be able to override.
        if (streams.size() > 1) {
            for (size_t i = 0; i < streams.size(); ++i) {
                UInt32 sLat = getU32(streams[i], kAudioStreamPropertyLatency,      kAudioDevicePropertyScopeInput);
                UInt32 sTrm = getU32(streams[i], kAudioStreamPropertyTerminalType, kAudioDevicePropertyScopeInput);
                perfLog("[Engine][lat:probe]    stream[%zu] id=%u latency=%u terminalType=0x%x\n",
                        i, streams[i], sLat, sTrm);
            }
        }

        // Enumerate alternate data sources on this device. Built-in mic
        // sometimes exposes multiple ("Internal microphone" vs
        // "Microphone array") and switching can bypass DSP chains.
        AudioObjectPropertyAddress addrSources = {
            kAudioDevicePropertyDataSources,
            kAudioDevicePropertyScopeInput,
            kAudioObjectPropertyElementMain
        };
        UInt32 srcSize = 0;
        if (AudioObjectGetPropertyDataSize(devId, &addrSources, 0, nullptr, &srcSize) == noErr && srcSize > 0) {
            std::vector<UInt32> sources(srcSize / sizeof(UInt32));
            AudioObjectGetPropertyData(devId, &addrSources, 0, nullptr, &srcSize, sources.data());
            UInt32 currentSource = getU32(devId, kAudioDevicePropertyDataSource, kAudioDevicePropertyScopeInput);
            juce::String srcList;
            for (size_t i = 0; i < sources.size(); ++i) {
                // Translate FourCC-encoded source ID to its name string.
                CFStringRef srcName = nullptr;
                AudioValueTranslation trans = { &sources[i], sizeof(UInt32),
                                                &srcName,    sizeof(CFStringRef) };
                AudioObjectPropertyAddress addrName = {
                    kAudioDevicePropertyDataSourceNameForIDCFString,
                    kAudioDevicePropertyScopeInput,
                    kAudioObjectPropertyElementMain
                };
                UInt32 transSize = sizeof(trans);
                AudioObjectGetPropertyData(devId, &addrName, 0, nullptr, &transSize, &trans);
                char nameBuf2[128] = "?";
                if (srcName) {
                    CFStringGetCString(srcName, nameBuf2, sizeof(nameBuf2), kCFStringEncodingUTF8);
                    CFRelease(srcName);
                }
                srcList << (i > 0 ? ", " : "") << "0x" << juce::String::toHexString((int) sources[i])
                        << "='" << nameBuf2 << "'"
                        << (sources[i] == currentSource ? "(current)" : "");
            }
            perfLog("[Engine][lat:probe]    data sources: %s\n", srcList.toRawUTF8());
        }
    }
}

// Diagnostic dump for the active device — JUCE's reported latencies +
// the actual buffer/SR + whether input and output are different
// devices (CoreAudio aggregate-device construction adds latency on top
// of single-device round-trip). Compare these numbers against Logic's
// "audio settings" pane on the same device to see whether the gap is
// the device's honest reported latency (then comp is the fix) or
// something we're adding in the JUCE/graph pipeline (then dig
// deeper).
static void logDeviceLatency(juce::AudioIODevice* device, const char* phase) {
    if (! device) return;
    int inLatencySamples  = device->getInputLatencyInSamples();
    int outLatencySamples = device->getOutputLatencyInSamples();
    double sr             = device->getCurrentSampleRate();
    int    bufSamples     = device->getCurrentBufferSizeSamples();
    double inMs    = sr > 0 ? (inLatencySamples  / sr) * 1000.0 : 0.0;
    double outMs   = sr > 0 ? (outLatencySamples / sr) * 1000.0 : 0.0;
    double bufMs   = sr > 0 ? (bufSamples        / sr) * 1000.0 : 0.0;
    double rtMs    = inMs + outMs;
    auto   inName  = device->getName();
    perfLog("[Engine][lat:%s] device='%s' sr=%.0f buf=%d (%.2f ms)\n",
            phase, inName.toRawUTF8(), sr, bufSamples, bufMs);
    perfLog("[Engine][lat:%s]   reported input=%d samples (%.2f ms), output=%d samples (%.2f ms), round-trip=%.2f ms\n",
            phase, inLatencySamples, inMs, outLatencySamples, outMs, rtMs);
    auto bs = device->getAvailableBufferSizes();
    juce::String sizes;
    for (int i = 0; i < bs.size(); ++i) sizes << bs[i] << (i + 1 < bs.size() ? "," : "");
    perfLog("[Engine][lat:%s]   available buffer sizes: %s\n",
            phase, sizes.toRawUTF8());
    dumpInputDeviceLatencyBreakdown();
}

AudioEngine::AudioEngine()
    : graph(std::make_unique<juce::AudioProcessorGraph>()),
      graphWrapper(std::make_unique<GraphWrapper>(*graph)),
      player(std::make_unique<juce::AudioProcessorPlayer>()) {
    juce::addDefaultFormatsToManager(formatManager);
    // The rate bridge wraps the graph wrapper; the player drives the bridge.
    rateBridge = std::make_unique<RateBridgeProcessor>(*graphWrapper);
}

void AudioEngine::attachProcessor() {
    if (!player || !rateBridge) return;
    rateBridge->setEngineSampleRate(requestedEngineRate);
    player->setProcessor(rateBridge.get());
}

AudioEngine::~AudioEngine() {
    shutdown();
}

void AudioEngine::initialise() {
    auto result = deviceManager.initialiseWithDefaultDevices(2, 2);
    if (result.isNotEmpty()) {
        perfLog("[Engine] Audio device error: %s\n", result.toRawUTF8());
        return;
    }

    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        logDeviceLatency(device, "init");
    }

    setupGraph();

    deviceManager.addChangeListener(this);
    deviceManager.addAudioCallback(&inputMeter);

    pluginScanNeeded = !loadPluginCache();
}

void AudioEngine::shutdown() {
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(&inputMeter);
    editorWindows.clear();
    deviceManager.removeAudioCallback(player.get());
    player->setProcessor(nullptr);
    graph->clear();
    tracks.clear();
    busses.clear();
}

void AudioEngine::pauseDeviceProcessing() {
    // Detach the processor so the device callback outputs silence while
    // something else (the offline renderer) drives the graph directly.
    if (player) player->setProcessor(nullptr);
}

void AudioEngine::resumeDeviceProcessing() {
    if (player && graphWrapper) attachProcessor();
}

void AudioEngine::setupGraph() {
    auto* device = deviceManager.getCurrentAudioDevice();
    if (!device) return;

    int numInputs = device->getActiveInputChannels().countNumberOfSetBits();
    perfLog("[Engine] Audio inputs: %d active channels\n", numInputs);
    auto inputNames = device->getInputChannelNames();
    for (int i = 0; i < inputNames.size(); ++i)
        perfLog("[Engine]   Input %d: %s\n", i, inputNames[i].toRawUTF8());
    if (numInputs == 0) numInputs = 2;  // fallback
    graph->setPlayConfigDetails(
        numInputs, 2,
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

    auto audioInputNode = graph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    audioInputNodeId = audioInputNode->nodeID;

    // Master output gain (between track/bus outputs and audio output)
    masterGainNode = graph->addNode(std::make_unique<GainProcessor>());

    // GraphWrapper sits between the player and graph — handles per-buffer
    // MIDI scheduling from the arrangement before the graph processes
    graphWrapper->setPlayConfigDetails(numInputs, 2,
        device->getCurrentSampleRate(),
        device->getCurrentBufferSizeSamples());
    graphWrapper->prepareToPlay(
        device->getCurrentSampleRate(),
        device->getCurrentBufferSizeSamples());
    attachProcessor();
    deviceManager.addAudioCallback(player.get());
}

void AudioEngine::rebuildGraph() {
    auto* device = deviceManager.getCurrentAudioDevice();
    if (!device) return;

    perfLog("[Engine] Rebuilding graph for device: %s\n", device->getName().toRawUTF8());

    // Stop audio processing
    deviceManager.removeAudioCallback(player.get());
    player->setProcessor(nullptr);

    // Remove old IO nodes and master gain (track/bus nodes remain)
    graph->removeNode(midiInputNodeId);
    graph->removeNode(audioOutputNodeId);
    graph->removeNode(audioInputNodeId);
    if (masterGainNode) graph->removeNode(masterGainNode->nodeID);

    // Reconfigure graph for new device
    int numInputs = device->getActiveInputChannels().countNumberOfSetBits();
    perfLog("[Engine] Audio inputs: %d active channels\n", numInputs);
    if (numInputs == 0) numInputs = 2;

    graph->setPlayConfigDetails(
        numInputs, 2,
        device->getCurrentSampleRate(),
        device->getCurrentBufferSizeSamples());
    graph->prepareToPlay(
        device->getCurrentSampleRate(),
        device->getCurrentBufferSizeSamples());

    // Recreate IO nodes
    auto midiInputNode = graph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
    midiInputNodeId = midiInputNode->nodeID;

    auto audioOutputNode = graph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    audioOutputNodeId = audioOutputNode->nodeID;

    auto audioInputNode = graph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    audioInputNodeId = audioInputNode->nodeID;

    masterGainNode = graph->addNode(std::make_unique<GainProcessor>());

    // Restart audio processing via GraphWrapper
    graphWrapper->setPlayConfigDetails(numInputs, 2,
        device->getCurrentSampleRate(),
        device->getCurrentBufferSizeSamples());
    graphWrapper->prepareToPlay(
        device->getCurrentSampleRate(),
        device->getCurrentBufferSizeSamples());
    attachProcessor();
    deviceManager.addAudioCallback(player.get());

    // Rewire all connections with new IO nodes
    rebuildConnections();

    perfLog("[Engine] Graph rebuilt: %d inputs, sr=%.0f, buf=%d\n",
            numInputs, device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    logDeviceLatency(device, "rebuild");
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &deviceManager) {
        auto* device = deviceManager.getCurrentAudioDevice();
        perfLog("[Engine] Device change notification (device=%s, tracks=%d)\n",
                device ? device->getName().toRawUTF8() : "null", (int)tracks.size());
        if (!device) {
            // Device is mid-transition (old closed, new not yet open).
            // Stop processing and retry shortly — JUCE may not send another notification.
            deviceManager.removeAudioCallback(player.get());
            player->setProcessor(nullptr);
            perfLog("[Engine] Audio device transitioning — will retry\n");
            juce::MessageManager::callAsync([this]() {
                auto* dev = deviceManager.getCurrentAudioDevice();
                if (dev) {
                    perfLog("[Engine] Device available after retry: %s\n", dev->getName().toRawUTF8());
                    applyEngineRate();
                } else {
                    perfLog("[Engine] Device still null after retry\n");
                }
            });
            return;
        }
        // Re-assert the engine rate on the (possibly new) device before
        // rebuilding — a freshly plugged device may default to another rate.
        applyEngineRate();
    }
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
        // Use explicit paths to avoid permission prompts for ~/Documents etc.
        juce::FileSearchPath auPaths;
        auPaths.add(juce::File("/Library/Audio/Plug-Ins/Components"));
        auPaths.add(juce::File(juce::File::getSpecialLocation(
            juce::File::userHomeDirectory).getFullPathName() + "/Library/Audio/Plug-Ins/Components"));
        juce::PluginDirectoryScanner scanner(
            knownPlugins, *format, auPaths, true, juce::File());
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

// --- UUID helpers ---

juce::String AudioEngine::generateId() {
    return juce::Uuid().toString();
}

juce::String AudioEngine::findTrackId(const juce::String& name) const {
    for (auto& [id, track] : tracks)
        if (track.name == name) return id;
    return {};
}

juce::String AudioEngine::findBusId(const juce::String& name) const {
    for (auto& [id, bus] : busses)
        if (bus.name == name) return id;
    return {};
}

// --- Track management ---

juce::String AudioEngine::createTrack(const juce::String& trackName) {
    Track track;
    track.name = trackName;
    track.outputGainNode = graph->addNode(std::make_unique<GainProcessor>());
    auto uuid = generateId();
    tracks[uuid] = std::move(track);
    perfLog("[Engine] Created track: %s (id=%s)\n", trackName.toRawUTF8(), uuid.toRawUTF8());
    return uuid;
}

void AudioEngine::createTrackWithId(const juce::String& id, const juce::String& trackName) {
    Track track;
    track.name = trackName;
    track.outputGainNode = graph->addNode(std::make_unique<GainProcessor>());
    track.midiSourceNode = graph->addNode(std::make_unique<MidiSourceNode>());
    tracks[id] = std::move(track);
    graphWrapper->registerTrackMidiSource(id,
        dynamic_cast<MidiSourceNode*>(tracks[id].midiSourceNode->getProcessor()));
    perfLog("[Engine] Created track: %s (id=%s)\n", trackName.toRawUTF8(), id.toRawUTF8());
}

void AudioEngine::createAudioInputTrackWithId(const juce::String& id, const juce::String& name,
                                               int inputChannelStart, int inputChannelCount) {
    Track track;
    track.name = name;
    track.sourceType = TrackSourceType::AudioInput;
    track.inputChannelStart = inputChannelStart;
    track.inputChannelCount = inputChannelCount;
    track.midiEnabled = false;
    track.outputGainNode = graph->addNode(std::make_unique<GainProcessor>());
    track.midiSourceNode = graph->addNode(std::make_unique<MidiSourceNode>());
    track.audioFileNode = graph->addNode(std::make_unique<AudioFileNode>());
    tracks[id] = std::move(track);
    graphWrapper->registerTrackMidiSource(id,
        dynamic_cast<MidiSourceNode*>(tracks[id].midiSourceNode->getProcessor()));
    graphWrapper->registerTrackAudioFileNode(id,
        dynamic_cast<AudioFileNode*>(tracks[id].audioFileNode->getProcessor()));
    rebuildConnections();
    perfLog("[Engine] Created audio input track: %s (id=%s, ch=%d, count=%d)\n",
            name.toRawUTF8(), id.toRawUTF8(), inputChannelStart, inputChannelCount);
}

void AudioEngine::removeTrack(const juce::String& trackId) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;

    if (it->second.instrumentNode)
        graph->removeNode(it->second.instrumentNode->nodeID);
    for (auto& fx : it->second.effects)
        if (fx.node) graph->removeNode(fx.node->nodeID);
    for (auto& send : it->second.sends)
        if (send.gainNode) graph->removeNode(send.gainNode->nodeID);
    if (it->second.outputGainNode)
        graph->removeNode(it->second.outputGainNode->nodeID);
    if (it->second.midiSourceNode) {
        graphWrapper->unregisterTrackMidiSource(trackId);
        graph->removeNode(it->second.midiSourceNode->nodeID);
    }
    if (it->second.audioFileNode) {
        graphWrapper->unregisterTrackAudioFileNode(trackId);
        graph->removeNode(it->second.audioFileNode->nodeID);
    }

    tracks.erase(it);
    rebuildConnections();
    perfLog("[Engine] Removed track: %s\n", trackId.toRawUTF8());
}

bool AudioEngine::addTrackInstrument(const juce::String& trackId, const juce::String& pluginName,
                                      LoadCallback onLoaded) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) {
        perfLog("[Engine] Track not found: %s\n", trackId.toRawUTF8());
        return false;
    }

    auto desc = findPluginDescription(pluginName);
    if (desc.name.isEmpty()) {
        perfLog("[Engine] Plugin not found: %s\n", pluginName.toRawUTF8());
        return false;
    }

    it->second.instrumentPluginName = pluginName;
    perfLog("[Engine] Loading instrument: %s -> track \"%s\"\n",
            desc.name.toRawUTF8(), trackId.toRawUTF8());

    formatManager.createPluginInstanceAsync(
        desc, graph->getSampleRate(), graph->getBlockSize(),
        [this, trackId, desc, onLoaded](std::unique_ptr<juce::AudioPluginInstance> instance,
                                         const juce::String& error) {
            if (!instance) {
                perfLog("[Engine] FAILED to load: %s\n", error.toRawUTF8());
                return;
            }
            auto it = tracks.find(trackId);
            if (it == tracks.end()) return;
            it->second.instrumentNode = graph->addNode(std::move(instance));
            rebuildConnections();
            perfLog("[Engine] Loaded instrument: %s in track \"%s\"\n",
                    desc.name.toRawUTF8(), it->second.name.toRawUTF8());
            if (onLoaded) onLoaded();
        });

    return true;
}

bool AudioEngine::addEffectToList(std::vector<EffectNode>& effects, const juce::String& parentId,
                                   const juce::String& effectId, const juce::String& pluginName,
                                   LoadCallback onLoaded) {
    auto desc = findPluginDescription(pluginName);
    if (desc.name.isEmpty()) {
        perfLog("[Engine] Plugin not found: %s\n", pluginName.toRawUTF8());
        return false;
    }

    effects.push_back({ effectId, pluginName, nullptr });
    perfLog("[Engine] Loading effect: %s -> \"%s\"\n",
            desc.name.toRawUTF8(), parentId.toRawUTF8());

    formatManager.createPluginInstanceAsync(
        desc, graph->getSampleRate(), graph->getBlockSize(),
        [this, parentId, effectId, desc, onLoaded](std::unique_ptr<juce::AudioPluginInstance> instance,
                                                    const juce::String& error) {
            if (!instance) {
                perfLog("[Engine] FAILED to load: %s\n", error.toRawUTF8());
                return;
            }
            std::vector<EffectNode>* efx = nullptr;
            if (parentId == "Output") {
                efx = &masterEffects;
            } else {
                auto tit = tracks.find(parentId);
                if (tit != tracks.end()) efx = &tit->second.effects;
                else {
                    auto bit = busses.find(parentId);
                    if (bit != busses.end()) efx = &bit->second.effects;
                }
            }
            if (!efx) return;
            // Find by ID instead of index — safe against vector modifications
            for (auto& node : *efx) {
                if (node.id == effectId) {
                    auto loadedName = instance->getName();
                    node.node = graph->addNode(std::move(instance));
                    node.pluginName = loadedName;
                    rebuildConnections();
                    perfLog("[Engine] Loaded effect: %s in \"%s\"\n",
                            loadedName.toRawUTF8(), parentId.toRawUTF8());
                    if (onLoaded) onLoaded();
                    return;
                }
            }
            perfLog("[Engine] Effect slot gone before load completed: %s\n", effectId.toRawUTF8());
        });

    return true;
}

void AudioEngine::removeEffectFromList(std::vector<EffectNode>& effects, const juce::String& parentId,
                                        const juce::String& effectId) {
    for (auto it = effects.begin(); it != effects.end(); ++it) {
        if (it->id == effectId) {
            auto name = it->pluginName;
            if (it->node)
                graph->removeNode(it->node->nodeID);
            effects.erase(it);
            rebuildConnections();
            perfLog("[Engine] Removed effect \"%s\" from \"%s\"\n",
                    name.toRawUTF8(), parentId.toRawUTF8());
            return;
        }
    }
}

bool AudioEngine::addEffect(const juce::String& parentId, const juce::String& effectId,
                              const juce::String& pluginName, LoadCallback onLoaded) {
    if (parentId == "Output")
        return addEffectToList(masterEffects, parentId, effectId, pluginName, onLoaded);
    auto tit = tracks.find(parentId);
    if (tit != tracks.end())
        return addEffectToList(tit->second.effects, parentId, effectId, pluginName, onLoaded);
    auto bit = busses.find(parentId);
    if (bit != busses.end())
        return addEffectToList(bit->second.effects, parentId, effectId, pluginName, onLoaded);
    perfLog("[Engine] Parent not found: %s\n", parentId.toRawUTF8());
    return false;
}

void AudioEngine::removeEffect(const juce::String& parentId, const juce::String& effectId) {
    if (parentId == "Output") {
        removeEffectFromList(masterEffects, parentId, effectId);
        return;
    }
    auto tit = tracks.find(parentId);
    if (tit != tracks.end()) {
        removeEffectFromList(tit->second.effects, parentId, effectId);
        return;
    }
    auto bit = busses.find(parentId);
    if (bit != busses.end())
        removeEffectFromList(bit->second.effects, parentId, effectId);
}

float AudioEngine::getTrackPeakLevel(const juce::String& trackId) const {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return 0.0f;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return proc->getPeakLevel();
    return 0.0f;
}

std::pair<float, float> AudioEngine::getTrackPeakLevelStereo(const juce::String& trackId) const {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return { 0.0f, 0.0f };
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return { proc->getPeakL(), proc->getPeakR() };
    return { 0.0f, 0.0f };
}

void AudioEngine::removeTrackInstrument(const juce::String& trackId) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    if (it->second.instrumentNode) {
        graph->removeNode(it->second.instrumentNode->nodeID);
        it->second.instrumentNode = nullptr;
        it->second.instrumentPluginName = "";
        rebuildConnections();
        perfLog("[Engine] Removed instrument from track \"%s\"\n", trackId.toRawUTF8());
    }
}


void AudioEngine::setTrackMidiEnabled(const juce::String& trackId, bool enabled) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    if (it->second.midiEnabled == enabled) return;

    it->second.midiEnabled = enabled;

    // Audio input tracks don't use midiEnabled — audioEnabled controls them
    if (it->second.sourceType == TrackSourceType::AudioInput) return;

    // If instrument hasn't loaded yet, the flag is stored and
    // rebuildConnections will use it when the instrument loads.
    if (!it->second.instrumentNode) return;

    juce::AudioProcessorGraph::Connection midiConn = {
        { midiInputNodeId, juce::AudioProcessorGraph::midiChannelIndex },
        { it->second.instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex }
    };

    if (enabled) {
        graph->addConnection(midiConn);
    } else {
        // Send all-notes-off before disconnecting MIDI
        for (int ch = 1; ch <= 16; ++ch) {
            auto msg = juce::MidiMessage::allNotesOff(ch);
            msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
            player->getMidiMessageCollector().addMessageToQueue(msg);
        }
        graph->removeConnection(midiConn);
    }

    perfLog("[Engine] MIDI %s for track \"%s\"\n",
            enabled ? "enabled" : "disabled", trackId.toRawUTF8());
}

void AudioEngine::setTrackAudioEnabled(const juce::String& trackId, bool enabled) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    if (it->second.audioEnabled == enabled) return;
    it->second.audioEnabled = enabled;
    rebuildConnections();
    perfLog("[Engine] Audio %s for track \"%s\"\n",
            enabled ? "enabled" : "disabled", trackId.toRawUTF8());
}

void AudioEngine::setTrackInputMonitoring(const juce::String& trackId, bool enabled) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    if (it->second.inputMonitoring == enabled) return;
    it->second.inputMonitoring = enabled;

    // For INSTRUMENT tracks, "input monitoring" means "this plugin
    // receives live MIDI." Drive the existing midiEnabled flag that
    // gates the midiInputNode → instrumentNode graph connection.
    // For audio tracks, midiEnabled is unused and inputMonitoring
    // gates audio-in → output wiring via rebuildConnections.
    // See docs/LIVE_INPUT_AND_FOCUS.md.
    if (it->second.sourceType == TrackSourceType::Instrument) {
        if (!enabled) {
            // Flush any hanging notes before the connection is removed.
            for (int ch = 1; ch <= 16; ++ch) {
                auto msg = juce::MidiMessage::allNotesOff(ch);
                msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
                player->getMidiMessageCollector().addMessageToQueue(msg);
            }
        }
        it->second.midiEnabled = enabled;
    }

    rebuildConnections();
    perfLog("[Engine] Input monitoring %s for track \"%s\"\n",
            enabled ? "enabled" : "disabled", trackId.toRawUTF8());
}

void AudioEngine::setTrackMuted(const juce::String& trackId, bool muted) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    it->second.muted = muted;
    updateMuteStates();
}

void AudioEngine::setTrackSoloed(const juce::String& trackId, bool soloed) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    it->second.soloed = soloed;
    updateMuteStates();
}

void AudioEngine::setTrackInputChannels(const juce::String& trackId, int start, int count) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    if (it->second.inputChannelStart == start && it->second.inputChannelCount == count) return;
    it->second.inputChannelStart = start;
    it->second.inputChannelCount = count;
    rebuildConnections();
    perfLog("[Engine] Set input channels for track \"%s\": start=%d count=%d\n",
            trackId.toRawUTF8(), start, count);
}

void AudioEngine::setTrackGain(const juce::String& trackId, float gain) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        proc->setGain(gain);
}

float AudioEngine::getTrackGain(const juce::String& trackId) const {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return 0.0f;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return proc->getGain();
    return 0.0f;
}

void AudioEngine::renameTrack(const juce::String& trackId, const juce::String& newName) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    if (it->second.name == newName) return;
    auto oldName = it->second.name;
    it->second.name = newName;
    perfLog("[Engine] Renamed track \"%s\" -> \"%s\"\n", oldName.toRawUTF8(), newName.toRawUTF8());
}

void AudioEngine::setTrackOutputTarget(const juce::String& trackId, const juce::String& target) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    if (it->second.outputTarget == target) return;
    it->second.outputTarget = target;
    rebuildConnections();
}

void AudioEngine::setBusOutputTarget(const juce::String& busId, const juce::String& target) {
    auto it = busses.find(busId);
    if (it == busses.end()) return;
    if (it->second.outputTarget == target) return;
    it->second.outputTarget = target;
    rebuildConnections();
}

void AudioEngine::renameBus(const juce::String& busId, const juce::String& newName) {
    auto it = busses.find(busId);
    if (it == busses.end()) return;
    if (it->second.name == newName) return;
    auto oldName = it->second.name;
    it->second.name = newName;
    perfLog("[Engine] Renamed bus \"%s\" -> \"%s\"\n", oldName.toRawUTF8(), newName.toRawUTF8());
}

void AudioEngine::clearAllTracks() {
    editorWindows.clear();
    graphWrapper->clearTrackMidiSources();
    graphWrapper->clearTrackAudioFileNodes();
    for (auto& [id, track] : tracks) {
        if (track.instrumentNode)
            graph->removeNode(track.instrumentNode->nodeID);
        if (track.midiSourceNode)
            graph->removeNode(track.midiSourceNode->nodeID);
        if (track.audioFileNode)
            graph->removeNode(track.audioFileNode->nodeID);
        for (auto& fx : track.effects)
            if (fx.node) graph->removeNode(fx.node->nodeID);
        for (auto& send : track.sends)
            if (send.gainNode) graph->removeNode(send.gainNode->nodeID);
        if (track.outputGainNode)
            graph->removeNode(track.outputGainNode->nodeID);
    }
    tracks.clear();
    rebuildConnections();
}

// --- Bus management ---

juce::String AudioEngine::createBus(const juce::String& busName) {
    Bus bus;
    bus.name = busName;
    bus.outputGainNode = graph->addNode(std::make_unique<GainProcessor>());
    auto uuid = generateId();
    busses[uuid] = std::move(bus);
    perfLog("[Engine] Created bus: %s (id=%s)\n", busName.toRawUTF8(), uuid.toRawUTF8());
    return uuid;
}

void AudioEngine::createBusWithId(const juce::String& id, const juce::String& busName) {
    Bus bus;
    bus.name = busName;
    bus.outputGainNode = graph->addNode(std::make_unique<GainProcessor>());
    busses[id] = std::move(bus);
    perfLog("[Engine] Created bus: %s (id=%s)\n", busName.toRawUTF8(), id.toRawUTF8());
}

void AudioEngine::removeBus(const juce::String& busId) {
    auto it = busses.find(busId);
    if (it == busses.end()) return;

    for (auto& fx : it->second.effects)
        if (fx.node) graph->removeNode(fx.node->nodeID);
    if (it->second.outputGainNode)
        graph->removeNode(it->second.outputGainNode->nodeID);

    busses.erase(it);
    rebuildConnections();
    perfLog("[Engine] Removed bus: %s\n", busId.toRawUTF8());
}


void AudioEngine::setBusGain(const juce::String& busId, float gain) {
    auto it = busses.find(busId);
    if (it == busses.end()) return;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        proc->setGain(gain);
}

void AudioEngine::setBusAudioEnabled(const juce::String& busId, bool enabled) {
    auto it = busses.find(busId);
    if (it == busses.end()) return;
    if (it->second.audioEnabled == enabled) return;
    it->second.audioEnabled = enabled;
    rebuildConnections();
    perfLog("[Engine] Audio %s for bus \"%s\"\n",
            enabled ? "enabled" : "disabled", busId.toRawUTF8());
}

void AudioEngine::clearAllBusses() {
    for (auto& [id, bus] : busses) {
        for (auto& fx : bus.effects)
            if (fx.node) graph->removeNode(fx.node->nodeID);
        if (bus.outputGainNode)
            graph->removeNode(bus.outputGainNode->nodeID);
    }
    busses.clear();
    rebuildConnections();
}

// --- Sends ---

void AudioEngine::addSend(const juce::String& trackId, const juce::String& busId,
                          const juce::String& sendId, float gain) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;

    auto gainNode = graph->addNode(std::make_unique<GainProcessor>());
    if (auto* proc = dynamic_cast<GainProcessor*>(gainNode->getProcessor()))
        proc->setGain(gain);

    it->second.sends.push_back({ sendId, busId, /*preFader*/ false, gainNode });
    rebuildConnections();
    perfLog("[Engine] Added send: track \"%s\" -> bus \"%s\" (id=%s, gain %.2f)\n",
            trackId.toRawUTF8(), busId.toRawUTF8(), sendId.toRawUTF8(), gain);
}

void AudioEngine::removeSend(const juce::String& sendId) {
    // Sends carry no trackId at deletion time (state already erased them), so
    // scan every track for the matching send id. Removing the graph node and
    // the SendNode entry is what actually kills the routing — without this the
    // phantom send gets re-wired on every rebuildConnections().
    for (auto& [trackId, track] : tracks) {
        auto& sends = track.sends;
        for (auto sit = sends.begin(); sit != sends.end(); ++sit) {
            if (sit->id != sendId) continue;
            if (sit->gainNode) graph->removeNode(sit->gainNode->nodeID);
            sends.erase(sit);
            rebuildConnections();
            perfLog("[Engine] Removed send: id=%s from track \"%s\"\n",
                    sendId.toRawUTF8(), trackId.toRawUTF8());
            return;
        }
    }
    perfLog("[Engine] removeSend: id=%s not found\n", sendId.toRawUTF8());
}

void AudioEngine::setSendGain(const juce::String& trackId, const juce::String& busId, float gain) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;
    for (auto& send : it->second.sends) {
        if (send.busId == busId) {
            if (auto* proc = dynamic_cast<GainProcessor*>(send.gainNode->getProcessor()))
                proc->setGain(gain);
            return;
        }
    }
}

void AudioEngine::setSendPreFader(const juce::String& sendId, bool preFader) {
    // Changing the tap point (pre vs post output gain) is a topology change,
    // so it must go through a rebuild.
    for (auto& [trackId, track] : tracks) {
        for (auto& send : track.sends) {
            if (send.id != sendId) continue;
            if (send.preFader == preFader) return;  // no-op
            send.preFader = preFader;
            rebuildConnections();
            return;
        }
    }
}

void AudioEngine::setSendMuted(const juce::String& sendId, bool muted) {
    // Send mute is just the send's own gain stage muting — no rewiring needed.
    for (auto& [trackId, track] : tracks) {
        for (auto& send : track.sends) {
            if (send.id != sendId) continue;
            if (auto* proc = dynamic_cast<GainProcessor*>(send.gainNode->getProcessor()))
                proc->setMuted(muted);
            return;
        }
    }
}

// --- Graph wiring ---

void AudioEngine::updateMuteStates() {
    bool anySoloed = false;
    for (auto& [id, track] : tracks)
        if (track.soloed) { anySoloed = true; break; }

    for (auto& [id, track] : tracks) {
        if (!track.outputGainNode) continue;
        auto* proc = dynamic_cast<GainProcessor*>(track.outputGainNode->getProcessor());
        if (!proc) continue;
        bool shouldMute = track.muted || (anySoloed && !track.soloed);
        proc->setMuted(shouldMute);
    }
}

void AudioEngine::rebuildConnections() {
    for (auto& conn : graph->getConnections())
        graph->removeConnection(conn);

    // Wire tracks
    for (auto& [trackId, track] : tracks) {
        // Determine the audio source for this track
        juce::AudioProcessorGraph::NodeID sourceNodeId;
        int prevNumOut = 2;
        bool hasSource = false;

        if (track.sourceType == TrackSourceType::AudioInput && track.audioEnabled) {
            // Audio track: wire live input (if assigned) and AudioFileNode for playback
            auto firstDestNodeId = track.outputGainNode->nodeID;
            if (!track.effects.empty() && track.effects[0].node)
                firstDestNodeId = track.effects[0].node->nodeID;

            // Live audio input (only if channels assigned and input monitoring on)
            if (track.inputMonitoring && track.inputChannelStart >= 0 && track.inputChannelCount > 0) {
                if (track.inputChannelCount == 1) {
                    graph->addConnection({{ audioInputNodeId, track.inputChannelStart }, { firstDestNodeId, 0 }});
                    graph->addConnection({{ audioInputNodeId, track.inputChannelStart }, { firstDestNodeId, 1 }});
                } else {
                    graph->addConnection({{ audioInputNodeId, track.inputChannelStart }, { firstDestNodeId, 0 }});
                    graph->addConnection({{ audioInputNodeId, track.inputChannelStart + 1 }, { firstDestNodeId, 1 }});
                }
            }

            // AudioFileNode for region playback (always connected)
            if (track.audioFileNode) {
                for (int ch = 0; ch < 2; ++ch)
                    graph->addConnection({{ track.audioFileNode->nodeID, ch }, { firstDestNodeId, ch }});
            }

            perfLog("[Engine] Wiring audio track \"%s\": input ch=%d count=%d, fileNode=%s\n",
                    track.name.toRawUTF8(), track.inputChannelStart, track.inputChannelCount,
                    track.audioFileNode ? "yes" : "no");

            hasSource = true;
        } else if (track.instrumentNode) {
            // Instrument track: wire MIDI and instrument audio
            auto* proc = track.instrumentNode->getProcessor();
            prevNumOut = std::min(proc->getTotalNumOutputChannels(), 2);
            perfLog("[Engine] Wiring track \"%s\": %s (%d out, midi=%s, audio=%s)\n",
                    track.name.toRawUTF8(), proc->getName().toRawUTF8(),
                    prevNumOut, track.midiEnabled ? "on" : "off",
                    track.audioEnabled ? "on" : "off");

            if (track.midiEnabled && track.audioEnabled) {
                // Live MIDI from external controllers
                bool ok = graph->addConnection({
                    { midiInputNodeId, juce::AudioProcessorGraph::midiChannelIndex },
                    { track.instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex }
                });
                perfLog("[Engine]   MIDI %s for \"%s\"\n",
                        ok ? "connected" : "FAILED", track.name.toRawUTF8());
            } else {
                perfLog("[Engine]   MIDI skipped for \"%s\"\n", track.name.toRawUTF8());
            }

            // Sequencer MIDI from arrangement (always connected when instrument exists)
            if (track.midiSourceNode && track.audioEnabled) {
                graph->addConnection({
                    { track.midiSourceNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                    { track.instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex }
                });
            }

            sourceNodeId = track.instrumentNode->nodeID;
            hasSource = true;
        }

        if (!hasSource) continue;

        // Audio path: source -> insert effects (for audio input, skip first effect since already connected)
        bool isAudioInput = (track.sourceType == TrackSourceType::AudioInput && track.inputChannelStart >= 0);
        auto prevNodeId = isAudioInput ? juce::AudioProcessorGraph::NodeID() : sourceNodeId;

        bool firstEffect = true;
        for (auto& fx : track.effects) {
            if (!fx.node) continue;
            if (isAudioInput && firstEffect) {
                // Audio input already connected to first effect above
                prevNodeId = fx.node->nodeID;
                prevNumOut = std::min(fx.node->getProcessor()->getTotalNumOutputChannels(), 2);
                firstEffect = false;
                continue;
            }
            firstEffect = false;
            for (int ch = 0; ch < prevNumOut; ++ch)
                graph->addConnection({ { prevNodeId, ch }, { fx.node->nodeID, ch } });
            prevNodeId = fx.node->nodeID;
            prevNumOut = std::min(fx.node->getProcessor()->getTotalNumOutputChannels(), 2);
        }

        // Fan out: last insert -> outputGainNode + send gainNodes
        if (track.outputGainNode) {
            // For audio input with no effects, audio input already connected to gain node above
            if (!(isAudioInput && track.effects.empty())) {
                for (int ch = 0; ch < prevNumOut; ++ch)
                    graph->addConnection({ { prevNodeId, ch }, { track.outputGainNode->nodeID, ch } });
            }
            // outputGainNode -> output target (master, bus, or none)
            if (track.audioEnabled && track.outputTarget != "none") {
                auto destNodeId = masterGainNode->nodeID;
                if (!track.outputTarget.isEmpty()) {
                    // Route to a bus instead of master
                    auto busIt = busses.find(track.outputTarget);
                    if (busIt != busses.end() && !busIt->second.effects.empty() && busIt->second.effects[0].node)
                        destNodeId = busIt->second.effects[0].node->nodeID;
                    else if (busIt != busses.end() && busIt->second.outputGainNode)
                        destNodeId = busIt->second.outputGainNode->nodeID;
                }
                for (int ch = 0; ch < 2; ++ch)
                    graph->addConnection({ { track.outputGainNode->nodeID, ch }, { destNodeId, ch } });
            }
        }

        // Send tap point depends on the send's fader mode:
        //   POST-FADER (default): tap the track's output gain node, so the send
        //     follows the track's mute / solo / fader. Uniform across all track
        //     types since outputGainNode already carries the summed, post-fader
        //     signal. This is what most people expect from an aux reverb/delay.
        //   PRE-FADER: tap the raw pre-fader signal, independent of mute/fader.
        //     - Instrument track, or audio track WITH insert effects: the
        //       effect-chain end (prevNodeId) already carries the summed signal.
        //     - Audio track with NO effects: there is no single pre-fader sum
        //       node (file playback + live input feed the output gain in
        //       parallel), so tap the raw sources -- file playback (always) +
        //       live input (when monitored) -- mirroring the output path.
        bool audioTrackNoFx = (track.sourceType == TrackSourceType::AudioInput
                               && track.audioEnabled && track.effects.empty());
        auto sendSourceId = prevNodeId;
        for (auto& send : track.sends) {
            if (!send.gainNode) continue;
            if (!send.preFader) {
                // Post-fader: tap the output gain (follows mute/solo/fader).
                if (track.outputGainNode)
                    for (int ch = 0; ch < 2; ++ch)
                        graph->addConnection({ { track.outputGainNode->nodeID, ch }, { send.gainNode->nodeID, ch } });
            } else if (audioTrackNoFx) {
                // Pre-fader, audio track, no effects: tap the raw sources.
                if (track.audioFileNode) {
                    for (int ch = 0; ch < 2; ++ch)
                        graph->addConnection({ { track.audioFileNode->nodeID, ch }, { send.gainNode->nodeID, ch } });
                }
                if (track.inputMonitoring && track.inputChannelStart >= 0 && track.inputChannelCount > 0) {
                    if (track.inputChannelCount == 1) {
                        graph->addConnection({{ audioInputNodeId, track.inputChannelStart }, { send.gainNode->nodeID, 0 }});
                        graph->addConnection({{ audioInputNodeId, track.inputChannelStart }, { send.gainNode->nodeID, 1 }});
                    } else {
                        graph->addConnection({{ audioInputNodeId, track.inputChannelStart }, { send.gainNode->nodeID, 0 }});
                        graph->addConnection({{ audioInputNodeId, track.inputChannelStart + 1 }, { send.gainNode->nodeID, 1 }});
                    }
                }
            } else {
                // Pre-fader, instrument or audio-with-effects: tap the chain end.
                for (int ch = 0; ch < prevNumOut; ++ch)
                    graph->addConnection({ { sendSourceId, ch }, { send.gainNode->nodeID, ch } });
            }
        }
    }

    // Wire busses
    for (auto& [busId, bus] : busses) {
        // Find the entry point for this bus: first effect node, or outputGainNode if no effects
        juce::AudioProcessorGraph::NodeID busEntryId;
        bool hasEntry = false;

        for (auto& fx : bus.effects) {
            if (fx.node) {
                busEntryId = fx.node->nodeID;
                hasEntry = true;
                break;
            }
        }
        if (!hasEntry && bus.outputGainNode) {
            busEntryId = bus.outputGainNode->nodeID;
            hasEntry = true;
        }
        if (!hasEntry) continue;

        perfLog("[Engine] Wiring bus \"%s\"\n", bus.name.toRawUTF8());

        // Connect all send gainNodes targeting this bus -> bus entry
        for (auto& [tId, track] : tracks) {
            for (auto& send : track.sends) {
                if (send.busId != busId || !send.gainNode) continue;
                for (int ch = 0; ch < 2; ++ch)
                    graph->addConnection({ { send.gainNode->nodeID, ch }, { busEntryId, ch } });
            }
        }

        // Chain bus effects
        auto prevNodeId = busEntryId;
        bool first = true;
        for (auto& fx : bus.effects) {
            if (!fx.node) continue;
            if (first) { first = false; continue; }  // skip first, it's the entry
            for (int ch = 0; ch < 2; ++ch)
                graph->addConnection({ { prevNodeId, ch }, { fx.node->nodeID, ch } });
            prevNodeId = fx.node->nodeID;
        }

        // Last effect -> bus outputGainNode -> output target
        if (bus.outputGainNode) {
            if (prevNodeId != bus.outputGainNode->nodeID) {
                for (int ch = 0; ch < 2; ++ch)
                    graph->addConnection({ { prevNodeId, ch }, { bus.outputGainNode->nodeID, ch } });
            }
            if (bus.audioEnabled && bus.outputTarget != "none") {
                auto destNodeId = masterGainNode->nodeID;
                if (!bus.outputTarget.isEmpty()) {
                    // Route to another bus
                    auto targetIt = busses.find(bus.outputTarget);
                    if (targetIt != busses.end() && !targetIt->second.effects.empty() && targetIt->second.effects[0].node)
                        destNodeId = targetIt->second.effects[0].node->nodeID;
                    else if (targetIt != busses.end() && targetIt->second.outputGainNode)
                        destNodeId = targetIt->second.outputGainNode->nodeID;
                }
                for (int ch = 0; ch < 2; ++ch)
                    graph->addConnection({ { bus.outputGainNode->nodeID, ch }, { destNodeId, ch } });
            }
        }
    }

    // Master output chain: masterGain -> [effects] -> audioOutput
    {
        auto prevNodeId = masterGainNode->nodeID;
        for (auto& fx : masterEffects) {
            if (fx.node) {
                for (int ch = 0; ch < 2; ++ch)
                    graph->addConnection({ { prevNodeId, ch }, { fx.node->nodeID, ch } });
                prevNodeId = fx.node->nodeID;
            }
        }
        if (masterAudioEnabled) {
            for (int ch = 0; ch < 2; ++ch)
                graph->addConnection({ { prevNodeId, ch }, { audioOutputNodeId, ch } });
        }
        perfLog("[Engine] Master output wired (%d total connections)\n",
                (int)graph->getConnections().size());
    }
}

// --- Processor access ---

// --- Query current state ---

std::vector<juce::String> AudioEngine::getTrackNames() const {
    std::vector<juce::String> names;
    for (auto& [id, track] : tracks) names.push_back(track.name);
    return names;
}

std::vector<juce::String> AudioEngine::getBusNames() const {
    std::vector<juce::String> names;
    for (auto& [id, bus] : busses) names.push_back(bus.name);
    return names;
}

juce::String AudioEngine::getTrackPluginName(const juce::String& trackId) const {
    auto it = tracks.find(trackId);
    return (it != tracks.end()) ? it->second.instrumentPluginName : juce::String();
}

bool AudioEngine::isTrackMidiEnabled(const juce::String& trackId) const {
    auto it = tracks.find(trackId);
    return it != tracks.end() && it->second.midiEnabled;
}

std::vector<AudioEngine::EffectInfo> AudioEngine::getTrackEffects(const juce::String& trackId) const {
    std::vector<EffectInfo> result;
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return result;
    for (auto& fx : it->second.effects)
        result.push_back({ fx.id, fx.pluginName });
    return result;
}

std::vector<AudioEngine::EffectInfo> AudioEngine::getBusEffects(const juce::String& busId) const {
    std::vector<EffectInfo> result;
    auto it = busses.find(busId);
    if (it == busses.end()) return result;
    for (auto& fx : it->second.effects)
        result.push_back({ fx.id, fx.pluginName });
    return result;
}

std::vector<juce::String> AudioEngine::getInputChannelNames() const {
    std::vector<juce::String> names;
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device) {
        auto channelNames = device->getInputChannelNames();
        for (auto& name : channelNames)
            names.push_back(name);
    }
    return names;
}

std::vector<float> AudioEngine::getInputPeakLevels() const {
    std::vector<float> levels;
    int nc = inputMeter.numChannels.load(std::memory_order_relaxed);
    for (int i = 0; i < nc; ++i)
        levels.push_back(inputMeter.peaks[i].load(std::memory_order_relaxed));
    return levels;
}

std::vector<AudioEngine::SendInfo> AudioEngine::getTrackSends(const juce::String& trackId) const {
    std::vector<SendInfo> result;
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return result;
    for (auto& send : it->second.sends) {
        float gain = 1.0f;
        float peak = 0.0f;
        if (auto* proc = dynamic_cast<GainProcessor*>(send.gainNode->getProcessor())) {
            gain = proc->getGain();
            peak = proc->getPeakLevel();
        }
        // Resolve busId back to display name
        juce::String busName;
        auto bit = busses.find(send.busId);
        if (bit != busses.end()) busName = bit->second.name;
        result.push_back({ busName, gain, peak });
    }
    return result;
}

float AudioEngine::getBusPeakLevel(const juce::String& busId) const {
    auto it = busses.find(busId);
    if (it == busses.end()) return 0.0f;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return proc->getPeakLevel();
    return 0.0f;
}

std::pair<float, float> AudioEngine::getBusPeakLevelStereo(const juce::String& busId) const {
    auto it = busses.find(busId);
    if (it == busses.end()) return { 0.0f, 0.0f };
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return { proc->getPeakL(), proc->getPeakR() };
    return { 0.0f, 0.0f };
}

float AudioEngine::getBusGain(const juce::String& busId) const {
    auto it = busses.find(busId);
    if (it == busses.end()) return 0.0f;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return proc->getGain();
    return 0.0f;
}

void AudioEngine::setMasterGain(float gain) {
    if (auto* proc = dynamic_cast<GainProcessor*>(masterGainNode->getProcessor()))
        proc->setGain(gain);
}

void AudioEngine::setMasterAudioEnabled(bool enabled) {
    if (masterAudioEnabled == enabled) return;
    masterAudioEnabled = enabled;
    rebuildConnections();
    perfLog("[Engine] Master audio %s\n", enabled ? "enabled" : "disabled");
}

float AudioEngine::getMasterGain() const {
    if (auto* proc = dynamic_cast<GainProcessor*>(masterGainNode->getProcessor()))
        return proc->getGain();
    return 1.0f;
}

float AudioEngine::getMasterPeakLevel() const {
    if (auto* proc = dynamic_cast<GainProcessor*>(masterGainNode->getProcessor()))
        return proc->getPeakLevel();
    return 0.0f;
}

std::pair<float, float> AudioEngine::getMasterPeakLevelStereo() const {
    if (auto* proc = dynamic_cast<GainProcessor*>(masterGainNode->getProcessor()))
        return { proc->getPeakL(), proc->getPeakR() };
    return { 0.0f, 0.0f };
}

std::vector<AudioEngine::EffectInfo> AudioEngine::getMasterEffects() const {
    std::vector<EffectInfo> result;
    for (auto& fx : masterEffects)
        result.push_back({ fx.id, fx.pluginName });
    return result;
}

// --- Processor access ---

juce::AudioProcessor* AudioEngine::getTrackInstrumentProcessor(const juce::String& trackId) const {
    auto it = tracks.find(trackId);
    if (it != tracks.end() && it->second.instrumentNode)
        return it->second.instrumentNode->getProcessor();
    return nullptr;
}

juce::AudioProcessor* AudioEngine::getEffectProcessor(const juce::String& parentId,
                                                             const juce::String& effectId) const {
    // Check master effects
    if (parentId == "Output") {
        for (auto& fx : masterEffects)
            if (fx.id == effectId && fx.node) return fx.node->getProcessor();
        return nullptr;
    }
    // Check tracks
    auto tit = tracks.find(parentId);
    if (tit != tracks.end()) {
        for (auto& fx : tit->second.effects)
            if (fx.id == effectId && fx.node) return fx.node->getProcessor();
        return nullptr;
    }
    // Check busses
    auto bit = busses.find(parentId);
    if (bit != busses.end()) {
        for (auto& fx : bit->second.effects)
            if (fx.id == effectId && fx.node) return fx.node->getProcessor();
    }
    return nullptr;
}

// Toolbar above the plugin editor with preset save/load
class PresetToolbar : public juce::Component {
public:
    PresetToolbar(AudioEngine::PresetCallbacks cbs) : callbacks(std::move(cbs)) {
        saveButton.setButtonText("Save");
        saveButton.onClick = [this] { showSaveDialog(); };
        addAndMakeVisible(saveButton);

        presetButton.setButtonText(callbacks.currentPresetName.isNotEmpty()
                                    ? callbacks.currentPresetName : "Default");
        presetButton.onClick = [this] { showPresetMenu(); };
        presetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252525));
        presetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdddddd));
        addAndMakeVisible(presetButton);
    }

    static constexpr int toolbarHeight = 44;
    static constexpr int titleHeight = 16;
    static constexpr int totalHeight = toolbarHeight + titleHeight;

    void resized() override {
        auto controlArea = getControlArea();
        auto group = controlArea;
        saveButton.setBounds(group.removeFromRight(50));
        group.removeFromRight(6);
        presetButton.setBounds(group);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1e1e1e));

        auto controlArea = getControlArea();
        auto borderArea = controlArea.toFloat().expanded(6, 4);

        // Bordered group
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawRoundedRectangle(borderArea, 4.0f, 1.0f);

        // Title above the bordered area
        g.setColour(juce::Colour(0xff666666));
        g.setFont(juce::Font(11.0f));
        g.drawText("Performance Presets",
                    controlArea.getX(), (int)borderArea.getY() - titleHeight - 1,
                    controlArea.getWidth(), titleHeight,
                    juce::Justification::centredLeft);
    }

    juce::Rectangle<int> getControlArea() const {
        int groupWidth = std::min(360, getWidth() - 24);
        int controlHeight = 28;
        // Center vertically in the toolbar area (below title), with equal space above and below
        int topOfToolbar = titleHeight;
        int availableHeight = getHeight() - topOfToolbar;
        int y = topOfToolbar + (availableHeight - controlHeight) / 2;
        int x = (getWidth() - groupWidth) / 2;
        return { x, y, groupWidth, controlHeight };
    }

private:
    AudioEngine::PresetCallbacks callbacks;
    juce::TextButton presetButton;
    juce::TextButton saveButton;

    void showPresetMenu() {
        if (!callbacks.listPresets) return;
        auto presets = callbacks.listPresets();
        if (presets.empty()) return;

        juce::PopupMenu menu;
        for (int i = 0; i < (int)presets.size(); ++i)
            menu.addItem(i + 1, presets[i]);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetButton),
            [this, presets](int result) {
                if (result == 0 || result - 1 >= (int)presets.size()) return;
                auto name = presets[result - 1];
                if (callbacks.loadPreset) callbacks.loadPreset(name);
                presetButton.setButtonText(name);
            });
    }

    void showSaveDialog() {
        if (!callbacks.savePreset) return;

        juce::StringArray names;
        if (callbacks.listPresets)
            for (auto& n : callbacks.listPresets())
                names.add(n);

        SaveAsDialog::show("Save Preset", presetButton.getButtonText(), names,
            [this](const juce::String& name) {
                if (callbacks.savePreset) callbacks.savePreset(name);
                presetButton.setButtonText(name);
            });
    }
};

// Plugin editor window with optional preset toolbar
class PluginEditorWindow : public juce::DocumentWindow {
public:
    PluginEditorWindow(const juce::String& key, const juce::String& displayTitle,
                       std::vector<std::unique_ptr<juce::DocumentWindow>>& windows,
                       juce::AudioProcessorEditor* editor,
                       AudioEngine::PresetCallbacks presetCallbacks)
        : DocumentWindow(displayTitle, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
          ownerWindows(windows) {
        editorKey = key;

        bool hasPresets = presetCallbacks.listPresets != nullptr;
        int toolbarHeight = hasPresets ? PresetToolbar::totalHeight : 0;

        auto* container = new juce::Component();
        container->setSize(editor->getWidth(), editor->getHeight() + toolbarHeight);

        editor->setTopLeftPosition(0, toolbarHeight);
        container->addAndMakeVisible(editor);

        if (hasPresets) {
            toolbar = std::make_unique<PresetToolbar>(std::move(presetCallbacks));
            toolbar->setBounds(0, 0, editor->getWidth(), toolbarHeight);
            container->addAndMakeVisible(toolbar.get());
            toolbar->toFront(false);
        }

        setContentOwned(container, true);
    }

    juce::String editorKey;  // unique ID for "already open" detection

    void closeButtonPressed() override {
        setVisible(false);
        juce::MessageManager::callAsync([this] {
            auto& wins = ownerWindows;
            wins.erase(std::remove_if(wins.begin(), wins.end(),
                [this](auto& w) { return w.get() == this; }), wins.end());
        });
    }

private:
    std::vector<std::unique_ptr<juce::DocumentWindow>>& ownerWindows;
    std::unique_ptr<PresetToolbar> toolbar;
};

void AudioEngine::openPluginEditor(const juce::String& parentId, const juce::String& effectId,
                                    const juce::String& title, PresetCallbacks presetCallbacks) {
    juce::AudioProcessor* processor = nullptr;
    if (effectId.isEmpty())
        processor = getTrackInstrumentProcessor(parentId);
    else
        processor = getEffectProcessor(parentId, effectId);

    if (!processor || !processor->hasEditor()) return;

    // Unique key: for instruments use trackId, for effects use effectId (which is the unique effect slot ID)
    auto editorKey = effectId.isEmpty() ? parentId : (parentId + "::" + effectId);

    // Check if editor is already open — bring to front
    for (auto& win : editorWindows) {
        auto* pew = dynamic_cast<PluginEditorWindow*>(win.get());
        if (pew && pew->editorKey == editorKey) {
            win->setVisible(true);
            win->toFront(true);
            return;
        }
    }

    auto* editor = processor->createEditor();
    if (!editor) {
        perfLog("[Engine] Plugin has no editor: %s\n", processor->getName().toRawUTF8());
        return;
    }

    auto displayTitle = title.isNotEmpty() ? title : processor->getName();
    auto window = std::make_unique<PluginEditorWindow>(
        editorKey, displayTitle, editorWindows, editor, std::move(presetCallbacks));
    window->setUsingNativeTitleBar(true);
    window->centreWithSize(window->getContentComponent()->getWidth(),
                            window->getContentComponent()->getHeight());
    window->setVisible(true);

    editorWindows.push_back(std::move(window));
}

void AudioEngine::closeTopPluginEditor() {
    // Close the most recently opened visible editor
    for (int i = (int)editorWindows.size() - 1; i >= 0; --i) {
        if (editorWindows[i]->isVisible()) {
            editorWindows[i]->setVisible(false);
            editorWindows.erase(editorWindows.begin() + i);
            return;
        }
    }
}

void AudioEngine::injectMidi(const juce::MidiMessage& message) {
    player->getMidiMessageCollector().addMessageToQueue(message);
}

void AudioEngine::setPlaybackState(bool playing, double bpm) {
    graphWrapper->setPlaying(playing);
    graphWrapper->setTempo(bpm);
}

void AudioEngine::setPlaybackBeatPosition(double beat) {
    graphWrapper->setBeatPosition(beat);
}

double AudioEngine::getPlaybackBeatPosition() const {
    return graphWrapper->getBeatPosition();
}

void AudioEngine::setPlaybackLoop(bool enabled, double start, double end) {
    graphWrapper->setLoop(enabled, start, end);
}

void AudioEngine::startPlayback(double beat, double bpm, bool loopOn, double loopS, double loopE) {
    graphWrapper->startPlayback(beat, bpm, loopOn, loopS, loopE);
}

void AudioEngine::stopPlayback() {
    graphWrapper->stopPlayback();
}

void AudioEngine::setArrangement(const Arrangement* arrangement) {
    graphWrapper->setArrangement(arrangement);
}

void AudioEngine::setRecording(bool recording) {
    graphWrapper->setRecording(recording);
}

RecordFIFO& AudioEngine::getRecordFIFO() {
    return graphWrapper->getRecordFIFO();
}

GraphWrapper::RecordDiag AudioEngine::readRecordDiag() const {
    return graphWrapper->readRecordDiag();
}

void AudioEngine::resetRecordDiag() {
    graphWrapper->resetRecordDiag();
}

void AudioEngine::setAudioRecordTargets(const std::vector<GraphWrapper::AudioRecordTarget>& targets) {
    graphWrapper->setAudioRecordTargets(targets);
}

void AudioEngine::clearAudioRecordTargets() {
    graphWrapper->clearAudioRecordTargets();
}

void AudioEngine::loadAudioFileForTrack(const juce::String& trackId, const juce::String& regionId,
                                         const juce::String& filePath, double recordTempo, int fileSampleRate) {
    auto it = tracks.find(trackId);
    if (it == tracks.end() || !it->second.audioFileNode) return;

    auto* afNode = dynamic_cast<AudioFileNode*>(it->second.audioFileNode->getProcessor());
    if (!afNode) return;

    if (afNode->loadFile(regionId, filePath, recordTempo, fileSampleRate))
        perfLog("[Engine] Loaded audio file for track %s region %s: %s\n",
                trackId.toRawUTF8(), regionId.toRawUTF8(), filePath.toRawUTF8());
    else
        perfLog("[Engine] Failed to load audio file: %s\n", filePath.toRawUTF8());
}

void AudioEngine::setMetronome(bool on, int beatsPerBar, float volume) {
    graphWrapper->setMetronome(on);
    graphWrapper->setBeatsPerBar(beatsPerBar);
    if (volume >= 0.0f)
        graphWrapper->setMetronomeVolume(volume);
}

double AudioEngine::getCurrentSampleRate() const {
    auto* device = deviceManager.getCurrentAudioDevice();
    return device ? device->getCurrentSampleRate() : 48000.0;
}

bool AudioEngine::deviceSupportsRate(double rate) const {
    auto* device = deviceManager.getCurrentAudioDevice();
    if (!device) return false;
    for (double r : device->getAvailableSampleRates())
        if (std::abs(r - rate) < 0.5) return true;
    return false;
}

void AudioEngine::setEngineSampleRate(double rate) {
    if (rate <= 0.0 || std::abs(rate - requestedEngineRate) < 0.5) return;  // no change
    requestedEngineRate = rate;
    perfLog("[Engine] Engine sample rate requested: %.0f\n", rate);
    applyEngineRate();
}

void AudioEngine::applyEngineRate() {
    auto* device = deviceManager.getCurrentAudioDevice();
    if (!device) return;

    // If the device isn't already at the engine rate and can do it, ask it to
    // switch. setAudioDeviceSetup restarts the device, which fires a change
    // notification → changeListenerCallback → applyEngineRate again; that
    // second pass finds the rate already correct and just rebuilds. Bounded
    // because we only request when the current rate differs AND is supported.
    if (std::abs(device->getCurrentSampleRate() - requestedEngineRate) > 0.5
        && deviceSupportsRate(requestedEngineRate)) {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.sampleRate = requestedEngineRate;
        auto err = deviceManager.setAudioDeviceSetup(setup, true);
        perfLog("[Engine] Requested device rate %.0f: %s\n",
                requestedEngineRate, err.isEmpty() ? "ok" : err.toRawUTF8());
        return;  // rebuild happens on the resulting change notification
    }

    // Device is at the engine rate, or can't do it (e.g. SCO 16k) — rebuild the
    // graph at whatever the device actually runs at. Phase 4 will insert the
    // async SRC for the can't-do-it case.
    if (std::abs(device->getCurrentSampleRate() - requestedEngineRate) > 0.5)
        perfLog("[Engine] Device %.0f can't match engine %.0f — running at device rate (SRC pending)\n",
                device->getCurrentSampleRate(), requestedEngineRate);
    rebuildGraph();
}
