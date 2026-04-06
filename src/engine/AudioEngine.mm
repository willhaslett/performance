#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
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

// --- Track management ---

void AudioEngine::createTrack(const juce::String& trackName) {
    Track track;
    track.outputGainNode = graph->addNode(std::make_unique<GainProcessor>());
    tracks[trackName] = std::move(track);
    perfLog("[Engine] Created track: %s\n", trackName.toRawUTF8());
}

void AudioEngine::removeTrack(const juce::String& trackName) {
    auto it = tracks.find(trackName);
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
    perfLog("[Engine] Removed track: %s\n", trackName.toRawUTF8());
}

bool AudioEngine::addTrackInstrument(const juce::String& trackName, const juce::String& pluginName,
                                      LoadCallback onLoaded) {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) {
        perfLog("[Engine] Track not found: %s\n", trackName.toRawUTF8());
        return false;
    }

    auto desc = findPluginDescription(pluginName);
    if (desc.name.isEmpty()) {
        perfLog("[Engine] Plugin not found: %s\n", pluginName.toRawUTF8());
        return false;
    }

    it->second.instrumentPluginName = pluginName;
    perfLog("[Engine] Loading instrument: %s -> track \"%s\"\n",
            desc.name.toRawUTF8(), trackName.toRawUTF8());

    formatManager.createPluginInstanceAsync(
        desc, graph->getSampleRate(), graph->getBlockSize(),
        [this, trackName, desc, onLoaded](std::unique_ptr<juce::AudioPluginInstance> instance,
                                           const juce::String& error) {
            if (!instance) {
                perfLog("[Engine] FAILED to load: %s\n", error.toRawUTF8());
                return;
            }
            auto it = tracks.find(trackName);
            if (it == tracks.end()) return;
            it->second.instrumentNode = graph->addNode(std::move(instance));
            rebuildConnections();
            perfLog("[Engine] Loaded instrument: %s in track \"%s\"\n",
                    desc.name.toRawUTF8(), trackName.toRawUTF8());
            if (onLoaded) onLoaded();
        });

    return true;
}

bool AudioEngine::addTrackEffect(const juce::String& trackName, const juce::String& effectName,
                                  const juce::String& pluginName, LoadCallback onLoaded) {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) {
        perfLog("[Engine] Track not found: %s\n", trackName.toRawUTF8());
        return false;
    }

    auto desc = findPluginDescription(pluginName);
    if (desc.name.isEmpty()) {
        perfLog("[Engine] Plugin not found: %s\n", pluginName.toRawUTF8());
        return false;
    }

    it->second.effects.push_back({ effectName, nullptr });
    auto effectIndex = it->second.effects.size() - 1;
    perfLog("[Engine] Loading effect: %s as \"%s\" -> track \"%s\"\n",
            desc.name.toRawUTF8(), effectName.toRawUTF8(), trackName.toRawUTF8());

    formatManager.createPluginInstanceAsync(
        desc, graph->getSampleRate(), graph->getBlockSize(),
        [this, trackName, effectIndex, desc, onLoaded](std::unique_ptr<juce::AudioPluginInstance> instance,
                                                        const juce::String& error) {
            if (!instance) {
                perfLog("[Engine] FAILED to load: %s\n", error.toRawUTF8());
                return;
            }
            auto it = tracks.find(trackName);
            if (it == tracks.end()) return;
            if (effectIndex >= it->second.effects.size()) return;
            it->second.effects[effectIndex].node = graph->addNode(std::move(instance));
            rebuildConnections();
            perfLog("[Engine] Loaded effect: %s as \"%s\" in track \"%s\"\n",
                    desc.name.toRawUTF8(), it->second.effects[effectIndex].name.toRawUTF8(),
                    trackName.toRawUTF8());
            if (onLoaded) onLoaded();
        });

    return true;
}

float AudioEngine::getTrackPeakLevel(const juce::String& trackName) const {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return 0.0f;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return proc->getPeakLevel();
    return 0.0f;
}

void AudioEngine::removeTrackInstrument(const juce::String& trackName) {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return;
    if (it->second.instrumentNode) {
        graph->removeNode(it->second.instrumentNode->nodeID);
        it->second.instrumentNode = nullptr;
        it->second.instrumentPluginName = "";
        rebuildConnections();
        perfLog("[Engine] Removed instrument from track \"%s\"\n", trackName.toRawUTF8());
    }
}

void AudioEngine::removeTrackEffect(const juce::String& trackName, const juce::String& effectName) {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return;
    auto& effects = it->second.effects;
    for (auto eit = effects.begin(); eit != effects.end(); ++eit) {
        if (eit->name == effectName) {
            if (eit->node)
                graph->removeNode(eit->node->nodeID);
            effects.erase(eit);
            rebuildConnections();
            perfLog("[Engine] Removed effect \"%s\" from track \"%s\"\n",
                    effectName.toRawUTF8(), trackName.toRawUTF8());
            return;
        }
    }
}

void AudioEngine::setTrackMidiEnabled(const juce::String& trackName, bool enabled) {
    auto it = tracks.find(trackName);
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
            enabled ? "enabled" : "disabled", trackName.toRawUTF8());
}

void AudioEngine::setTrackGain(const juce::String& trackName, float gain) {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        proc->setGain(gain);
}

float AudioEngine::getTrackGain(const juce::String& trackName) const {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return 0.0f;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return proc->getGain();
    return 0.0f;
}

void AudioEngine::clearAllTracks() {
    editorWindows.clear();
    for (auto& [name, track] : tracks) {
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

void AudioEngine::createBus(const juce::String& busName) {
    Bus bus;
    bus.outputGainNode = graph->addNode(std::make_unique<GainProcessor>());
    busses[busName] = std::move(bus);
    perfLog("[Engine] Created bus: %s\n", busName.toRawUTF8());
}

void AudioEngine::removeBus(const juce::String& busName) {
    auto it = busses.find(busName);
    if (it == busses.end()) return;

    for (auto& fx : it->second.effects)
        if (fx.node) graph->removeNode(fx.node->nodeID);
    if (it->second.outputGainNode)
        graph->removeNode(it->second.outputGainNode->nodeID);

    busses.erase(it);
    rebuildConnections();
    perfLog("[Engine] Removed bus: %s\n", busName.toRawUTF8());
}

bool AudioEngine::addBusEffect(const juce::String& busName, const juce::String& effectName,
                                const juce::String& pluginName, LoadCallback onLoaded) {
    auto it = busses.find(busName);
    if (it == busses.end()) {
        perfLog("[Engine] Bus not found: %s\n", busName.toRawUTF8());
        return false;
    }

    auto desc = findPluginDescription(pluginName);
    if (desc.name.isEmpty()) {
        perfLog("[Engine] Plugin not found: %s\n", pluginName.toRawUTF8());
        return false;
    }

    it->second.effects.push_back({ effectName, nullptr });
    auto effectIndex = it->second.effects.size() - 1;
    perfLog("[Engine] Loading effect: %s as \"%s\" -> bus \"%s\"\n",
            desc.name.toRawUTF8(), effectName.toRawUTF8(), busName.toRawUTF8());

    formatManager.createPluginInstanceAsync(
        desc, graph->getSampleRate(), graph->getBlockSize(),
        [this, busName, effectIndex, desc, onLoaded](std::unique_ptr<juce::AudioPluginInstance> instance,
                                                      const juce::String& error) {
            if (!instance) {
                perfLog("[Engine] FAILED to load: %s\n", error.toRawUTF8());
                return;
            }
            auto it = busses.find(busName);
            if (it == busses.end()) return;
            if (effectIndex >= it->second.effects.size()) return;
            it->second.effects[effectIndex].node = graph->addNode(std::move(instance));
            rebuildConnections();
            perfLog("[Engine] Loaded effect: %s as \"%s\" in bus \"%s\"\n",
                    desc.name.toRawUTF8(), it->second.effects[effectIndex].name.toRawUTF8(),
                    busName.toRawUTF8());
            if (onLoaded) onLoaded();
        });

    return true;
}

void AudioEngine::setBusGain(const juce::String& busName, float gain) {
    auto it = busses.find(busName);
    if (it == busses.end()) return;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        proc->setGain(gain);
}

void AudioEngine::clearAllBusses() {
    for (auto& [name, bus] : busses) {
        for (auto& fx : bus.effects)
            if (fx.node) graph->removeNode(fx.node->nodeID);
        if (bus.outputGainNode)
            graph->removeNode(bus.outputGainNode->nodeID);
    }
    busses.clear();
    rebuildConnections();
}

// --- Sends ---

void AudioEngine::addSend(const juce::String& trackName, const juce::String& busName, float gain) {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return;

    auto gainNode = graph->addNode(std::make_unique<GainProcessor>());
    if (auto* proc = dynamic_cast<GainProcessor*>(gainNode->getProcessor()))
        proc->setGain(gain);

    it->second.sends.push_back({ busName, gainNode });
    rebuildConnections();
    perfLog("[Engine] Added send: track \"%s\" -> bus \"%s\" (gain %.2f)\n",
            trackName.toRawUTF8(), busName.toRawUTF8(), gain);
}

void AudioEngine::setSendGain(const juce::String& trackName, const juce::String& busName, float gain) {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return;
    for (auto& send : it->second.sends) {
        if (send.busName == busName) {
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
    for (auto& [trackName, track] : tracks) {
        if (!track.instrumentNode) continue;

        auto* proc = track.instrumentNode->getProcessor();
        int numOut = std::min(proc->getTotalNumOutputChannels(), 2);
        perfLog("[Engine] Wiring track \"%s\": %s (%d out, midi=%s)\n",
                trackName.toRawUTF8(), proc->getName().toRawUTF8(),
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
            // outputGainNode -> audio output
            for (int ch = 0; ch < 2; ++ch)
                graph->addConnection({ { track.outputGainNode->nodeID, ch }, { audioOutputNodeId, ch } });
        }

        for (auto& send : track.sends) {
            if (!send.gainNode) continue;
            for (int ch = 0; ch < prevNumOut; ++ch)
                graph->addConnection({ { prevNodeId, ch }, { send.gainNode->nodeID, ch } });
        }
    }

    // Wire busses
    for (auto& [busName, bus] : busses) {
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

        perfLog("[Engine] Wiring bus \"%s\"\n", busName.toRawUTF8());

        // Connect all send gainNodes targeting this bus -> bus entry
        for (auto& [trackName, track] : tracks) {
            for (auto& send : track.sends) {
                if (send.busName != busName || !send.gainNode) continue;
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
                graph->addConnection({ { bus.outputGainNode->nodeID, ch }, { audioOutputNodeId, ch } });
        }
    }
}

// --- Processor access ---

// --- Query current state ---

std::vector<juce::String> AudioEngine::getTrackNames() const {
    std::vector<juce::String> names;
    for (auto& [name, _] : tracks) names.push_back(name);
    return names;
}

std::vector<juce::String> AudioEngine::getBusNames() const {
    std::vector<juce::String> names;
    for (auto& [name, _] : busses) names.push_back(name);
    return names;
}

juce::String AudioEngine::getTrackPluginName(const juce::String& trackName) const {
    auto it = tracks.find(trackName);
    return (it != tracks.end()) ? it->second.instrumentPluginName : juce::String();
}

bool AudioEngine::isTrackMidiEnabled(const juce::String& trackName) const {
    auto it = tracks.find(trackName);
    return it != tracks.end() && it->second.midiEnabled;
}

std::vector<AudioEngine::EffectInfo> AudioEngine::getTrackEffects(const juce::String& trackName) const {
    std::vector<EffectInfo> result;
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return result;
    for (auto& fx : it->second.effects)
        result.push_back({ fx.name, fx.node ? fx.node->getProcessor()->getName() : juce::String() });
    return result;
}

std::vector<AudioEngine::EffectInfo> AudioEngine::getBusEffects(const juce::String& busName) const {
    std::vector<EffectInfo> result;
    auto it = busses.find(busName);
    if (it == busses.end()) return result;
    for (auto& fx : it->second.effects)
        result.push_back({ fx.name, fx.node ? fx.node->getProcessor()->getName() : juce::String() });
    return result;
}

std::vector<AudioEngine::SendInfo> AudioEngine::getTrackSends(const juce::String& trackName) const {
    std::vector<SendInfo> result;
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return result;
    for (auto& send : it->second.sends) {
        float gain = 1.0f;
        if (auto* proc = dynamic_cast<GainProcessor*>(send.gainNode->getProcessor()))
            gain = proc->getGain();
        result.push_back({ send.busName, gain });
    }
    return result;
}

float AudioEngine::getBusPeakLevel(const juce::String& busName) const {
    auto it = busses.find(busName);
    if (it == busses.end()) return 0.0f;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return proc->getPeakLevel();
    return 0.0f;
}

float AudioEngine::getBusGain(const juce::String& busName) const {
    auto it = busses.find(busName);
    if (it == busses.end()) return 0.0f;
    if (auto* proc = dynamic_cast<GainProcessor*>(it->second.outputGainNode->getProcessor()))
        return proc->getGain();
    return 0.0f;
}

// --- Processor access ---

juce::AudioProcessor* AudioEngine::getTrackInstrumentProcessor(const juce::String& trackName) const {
    auto it = tracks.find(trackName);
    if (it != tracks.end() && it->second.instrumentNode)
        return it->second.instrumentNode->getProcessor();
    return nullptr;
}

juce::AudioProcessor* AudioEngine::getTrackEffectProcessor(const juce::String& trackName,
                                                             const juce::String& effectName) const {
    auto it = tracks.find(trackName);
    if (it == tracks.end()) return nullptr;
    for (auto& fx : it->second.effects) {
        if (fx.name == effectName && fx.node)
            return fx.node->getProcessor();
    }
    return nullptr;
}

class PluginEditorWindow : public juce::DocumentWindow {
public:
    PluginEditorWindow(const juce::String& name, std::vector<std::unique_ptr<juce::DocumentWindow>>& windows)
        : DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
          ownerWindows(windows) {}

    void closeButtonPressed() override {
        setVisible(false);
        // Defer removal to avoid deleting ourselves mid-callback
        juce::MessageManager::callAsync([this] {
            auto& wins = ownerWindows;
            wins.erase(std::remove_if(wins.begin(), wins.end(),
                [this](auto& w) { return w.get() == this; }), wins.end());
        });
    }

private:
    std::vector<std::unique_ptr<juce::DocumentWindow>>& ownerWindows;
};

void AudioEngine::openPluginEditor(const juce::String& trackName, const juce::String& effectName) {
    juce::AudioProcessor* processor = nullptr;
    if (effectName.isEmpty())
        processor = getTrackInstrumentProcessor(trackName);
    else
        processor = getTrackEffectProcessor(trackName, effectName);

    if (!processor || !processor->hasEditor()) return;

    // Check if editor is already open — bring to front
    for (auto& win : editorWindows) {
        if (win->getName() == processor->getName()) {
            win->setVisible(true);
            win->toFront(true);
            return;
        }
    }

    auto* editor = processor->createEditor();
    if (!editor) return;

    auto window = std::make_unique<PluginEditorWindow>(processor->getName(), editorWindows);
    window->setContentOwned(editor, true);
    window->setUsingNativeTitleBar(true);
    window->centreWithSize(editor->getWidth(), editor->getHeight());
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
