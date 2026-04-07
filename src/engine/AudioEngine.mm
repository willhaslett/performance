#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/Log.h"
#include "gui/SaveAsDialog.h"
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
    tracks.clear();
    busses.clear();
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

    // Master output gain (between track/bus outputs and audio output)
    masterGainNode = graph->addNode(std::make_unique<GainProcessor>());

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
    tracks[id] = std::move(track);
    perfLog("[Engine] Created track: %s (id=%s)\n", trackName.toRawUTF8(), id.toRawUTF8());
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
    auto oldName = it->second.name;
    it->second.name = newName;
    perfLog("[Engine] Renamed track \"%s\" -> \"%s\"\n", oldName.toRawUTF8(), newName.toRawUTF8());
}

void AudioEngine::renameBus(const juce::String& busId, const juce::String& newName) {
    auto it = busses.find(busId);
    if (it == busses.end()) return;
    auto oldName = it->second.name;
    it->second.name = newName;
    perfLog("[Engine] Renamed bus \"%s\" -> \"%s\"\n", oldName.toRawUTF8(), newName.toRawUTF8());
}

void AudioEngine::clearAllTracks() {
    editorWindows.clear();
    for (auto& [id, track] : tracks) {
        if (track.instrumentNode)
            graph->removeNode(track.instrumentNode->nodeID);
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

void AudioEngine::addSend(const juce::String& trackId, const juce::String& busId, float gain) {
    auto it = tracks.find(trackId);
    if (it == tracks.end()) return;

    auto gainNode = graph->addNode(std::make_unique<GainProcessor>());
    if (auto* proc = dynamic_cast<GainProcessor*>(gainNode->getProcessor()))
        proc->setGain(gain);

    it->second.sends.push_back({ busId, gainNode });
    rebuildConnections();
    perfLog("[Engine] Added send: track \"%s\" -> bus \"%s\" (gain %.2f)\n",
            trackId.toRawUTF8(), busId.toRawUTF8(), gain);
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

// --- Graph wiring ---

void AudioEngine::rebuildConnections() {
    for (auto& conn : graph->getConnections())
        graph->removeConnection(conn);

    // Wire tracks
    for (auto& [trackId, track] : tracks) {
        if (!track.instrumentNode) continue;

        auto* proc = track.instrumentNode->getProcessor();
        int numOut = std::min(proc->getTotalNumOutputChannels(), 2);
        perfLog("[Engine] Wiring track \"%s\": %s (%d out, midi=%s)\n",
                track.name.toRawUTF8(), proc->getName().toRawUTF8(),
                numOut, track.midiEnabled ? "on" : "off");

        // MIDI input -> instrument
        if (track.midiEnabled) {
            graph->addConnection({
                { midiInputNodeId, juce::AudioProcessorGraph::midiChannelIndex },
                { track.instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex }
            });
        }

        // Audio path: instrument -> insert effects
        auto prevNodeId = track.instrumentNode->nodeID;
        int prevNumOut = numOut;

        for (auto& fx : track.effects) {
            if (!fx.node) continue;
            for (int ch = 0; ch < prevNumOut; ++ch)
                graph->addConnection({ { prevNodeId, ch }, { fx.node->nodeID, ch } });
            prevNodeId = fx.node->nodeID;
            prevNumOut = std::min(fx.node->getProcessor()->getTotalNumOutputChannels(), 2);
        }

        // Fan out: last insert -> outputGainNode + send gainNodes
        if (track.outputGainNode) {
            for (int ch = 0; ch < prevNumOut; ++ch)
                graph->addConnection({ { prevNodeId, ch }, { track.outputGainNode->nodeID, ch } });
            // outputGainNode -> master gain
            for (int ch = 0; ch < 2; ++ch)
                graph->addConnection({ { track.outputGainNode->nodeID, ch }, { masterGainNode->nodeID, ch } });
        }

        for (auto& send : track.sends) {
            if (!send.gainNode) continue;
            for (int ch = 0; ch < prevNumOut; ++ch)
                graph->addConnection({ { prevNodeId, ch }, { send.gainNode->nodeID, ch } });
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

        // Last effect -> bus outputGainNode -> audio output
        if (bus.outputGainNode) {
            if (prevNodeId != bus.outputGainNode->nodeID) {
                for (int ch = 0; ch < 2; ++ch)
                    graph->addConnection({ { prevNodeId, ch }, { bus.outputGainNode->nodeID, ch } });
            }
            for (int ch = 0; ch < 2; ++ch)
                graph->addConnection({ { bus.outputGainNode->nodeID, ch }, { masterGainNode->nodeID, ch } });
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
        for (int ch = 0; ch < 2; ++ch)
            graph->addConnection({ { prevNodeId, ch }, { audioOutputNodeId, ch } });
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
