#include "gui/Sidebar.h"
#include <set>
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

Sidebar::Sidebar() {
    addAndMakeVisible(tree);
    startTimerHz(4);  // check active song highlight

    tree.setOnNodeRightClick([this](const std::string& type, const std::string& id, const std::string& label) {
        if (type == "song" && label != "Sandbox") {
            juce::PopupMenu menu;
            menu.addItem(1, "Delete Song");
            auto songId = id;
            menu.showMenuAsync(juce::PopupMenu::Options(),
                [this, songId](int result) {
                    if (result == 1 && onDeleteSong)
                        onDeleteSong(songId);
                });
        }
    });

    tree.setOnNodeClick([this](const std::string& type, const std::string& id, const std::string& label) {
        if (!state) return;
        perfLog("[Sidebar] Clicked %s: %s (%s)\n", type.c_str(), label.c_str(), id.c_str());

        if (type == "song" && onLoadSong) {
            selectedDeviceId.clear();
            onLoadSong(id);
        }
        else if (type == "audio_device") {
            selectedDeviceId = id;
            // id is "audio_both:DeviceName" — extract device name
            auto devName = id.substr(id.find(':') + 1);
            if (onAudioOutputSelected) onAudioOutputSelected(devName);
            if (onAudioInputSelected) onAudioInputSelected(devName);
        } else if (type == "audio_output" || type == "audio_output_active") {
            selectedDeviceId = id;
            // id is "audio_out:DeviceName"
            auto devName = id.substr(id.find(':') + 1);
            if (onAudioOutputSelected) onAudioOutputSelected(devName);
        } else if (type == "audio_input" || type == "audio_input_active") {
            selectedDeviceId = id;
            // id is "audio_in:DeviceName"
            auto devName = id.substr(id.find(':') + 1);
            if (onAudioInputSelected) onAudioInputSelected(devName);
        } else if (type == "map_device" || type == "map_unregistered") {
            selectedDeviceId = id;
            if (onMapSelected) {
                if (type == "map_device")
                    onMapSelected(id, "");
                else
                    onMapSelected("", id);  // id is port name for unregistered
            }
        } else if (type == "audio_buffer" && engineAPI) {
            // Show buffer size picker popup
            auto& dm = engineAPI->getDeviceManager();
            if (auto* device = dm.getCurrentAudioDevice()) {
                auto bufferSizes = device->getAvailableBufferSizes();
                int currentBuf = device->getCurrentBufferSizeSamples();
                juce::PopupMenu menu;
                for (int i = 0; i < bufferSizes.size(); ++i)
                    menu.addItem(i + 1, juce::String(bufferSizes[i]) + " samples",
                                 true, bufferSizes[i] == currentBuf);
                menu.showMenuAsync(juce::PopupMenu::Options(),
                    [this, bufferSizes](int result) {
                        if (result == 0) return;
                        int newSize = bufferSizes[result - 1];
                        auto& dm = engineAPI->getDeviceManager();
                        auto setup = dm.getAudioDeviceSetup();
                        setup.bufferSize = newSize;
                        auto err = dm.setAudioDeviceSetup(setup, true);
                        if (err.isEmpty()) {
                            // Verify and persist
                            if (auto* dev = dm.getCurrentAudioDevice())
                                perfLog("[Sidebar] Buffer size: requested=%d actual=%d\n",
                                        newSize, dev->getCurrentBufferSizeSamples());
                            if (state)
                                state->setConfig("audio_buffer_size", std::to_string(newSize));
                        } else {
                            perfLog("[Sidebar] Buffer size change failed: %s\n", err.toRawUTF8());
                        }
                        refreshTree();
                    });
            }
        } else if (type == "pane_content") {
            // id is "slot:content", e.g. "left:produce"
            auto colonPos = id.find(':');
            if (colonPos != std::string::npos && onPaneSelected) {
                auto slot = id.substr(0, colonPos);
                auto content = id.substr(colonPos + 1);
                onPaneSelected(slot, content);
                refreshTree();
            }
        }
    });
}

Sidebar::~Sidebar() {
    if (state && subscriptionId >= 0)
        state->events().unsubscribe(subscriptionId);
}

void Sidebar::setEngineAPI(EngineAPI* e) {
    engineAPI = e;
    refreshTree();
}

void Sidebar::setCoordinator(PerformanceCoordinator* c) {
    coordinator = c;
}

void Sidebar::setStateAPI(StateAPI* s) {
    if (state && subscriptionId >= 0)
        state->events().unsubscribe(subscriptionId);

    state = s;
    if (!state) return;

    // Subscribe to all state changes
    subscriptionId = state->events().subscribe([this](const StateEvent&) {
        // Flag for refresh — we may be on a non-message thread
        needsRefresh = true;
        juce::MessageManager::callAsync([this] {
            if (needsRefresh) {
                needsRefresh = false;
                refreshTree();
            }
        });
    });

    // Initial population
    refreshTree();
}

void Sidebar::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgPanel));

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)getWidth(), 0.0f, (float)getWidth(), (float)getHeight(), 1.0f);
}

void Sidebar::resized() {
    tree.setBounds(getLocalBounds().reduced(0, 4));
}

void Sidebar::timerCallback() {
    if (!state) return;
    // Highlight: prefer selected device/pane, fall back to active song
    std::string highlightId = selectedDeviceId;
    if (highlightId.empty()) {
        auto song = state->currentSong();
        highlightId = song ? song->id : "";
    }
    if (highlightId != lastHighlightedId) {
        lastHighlightedId = highlightId;
        tree.setHighlightedId(highlightId);
    }

    // Poll for MIDI device changes — rescan when count changes
    auto devices = juce::MidiInput::getAvailableDevices();
    int count = (int)devices.size();
    if (count != lastMidiDeviceCount) {
        lastMidiDeviceCount = count;
        if (coordinator) coordinator->refreshMidiDevices();
        refreshTree();
    }

    // Poll for audio device changes (output or input)
    if (engineAPI) {
        auto setup = engineAPI->getDeviceManager().getAudioDeviceSetup();
        if (setup.outputDeviceName != lastAudioOutputName ||
            setup.inputDeviceName != lastAudioInputName) {
            lastAudioOutputName = setup.outputDeviceName;
            lastAudioInputName = setup.inputDeviceName;
            refreshTree();
        }
    }
}

void Sidebar::refreshTree() {
    if (!state) return;

    std::vector<TreeNode> roots;

    // Songs — Sandbox always first, then user songs
    {
        TreeNode songsNode;
        songsNode.label = "Songs";
        songsNode.type = "category";

        // Sandbox always at the top
        for (auto& song : state->allSongs()) {
            if (song.name == "Sandbox") {
                TreeNode sandboxNode;
                sandboxNode.label = "Sandbox";
                sandboxNode.id = song.id;
                sandboxNode.type = "song";
                sandboxNode.isLeaf = true;
                songsNode.children.push_back(sandboxNode);
                break;
            }
        }

        // User songs
        for (auto& song : state->allSongs()) {
            if (song.name == "Sandbox") continue;
            TreeNode songNode;
            songNode.label = song.name;
            songNode.id = song.id;
            songNode.type = "song";
            songNode.isLeaf = true;
            songsNode.children.push_back(songNode);
        }
        roots.push_back(songsNode);
    }

    // Library — saved sounds grouped by instrument/effect
    {
        TreeNode libraryNode;
        libraryNode.label = "Library";
        libraryNode.type = "category";

        // Instruments (plugins that are instruments with saved presets)
        TreeNode instrumentsNode;
        instrumentsNode.label = "Instruments";
        instrumentsNode.type = "category";

        TreeNode effectsNode;
        effectsNode.label = "Effects";
        effectsNode.type = "category";

        for (auto& plugin : state->allPlugins()) {
            auto presets = state->presetsForPlugin(plugin.id);
            if (presets.empty()) continue;

            TreeNode pluginNode;
            pluginNode.label = plugin.name;
            pluginNode.type = "plugin";
            pluginNode.id = plugin.id;

            for (auto& preset : presets) {
                TreeNode presetLeaf;
                presetLeaf.label = preset->name;
                presetLeaf.id = preset->id;
                presetLeaf.type = "preset";
                presetLeaf.isLeaf = true;
                pluginNode.children.push_back(presetLeaf);
            }

            // Determine if instrument or effect from plugin description
            bool isInstrument = plugin.isInstrument;

            if (isInstrument)
                instrumentsNode.children.push_back(pluginNode);
            else
                effectsNode.children.push_back(pluginNode);
        }

        if (!instrumentsNode.children.empty())
            libraryNode.children.push_back(instrumentsNode);
        if (!effectsNode.children.empty())
            libraryNode.children.push_back(effectsNode);

        roots.push_back(libraryNode);
    }

    // Actions
    {
        TreeNode actionsNode;
        actionsNode.label = "Actions";
        actionsNode.type = "category";

        for (auto& action : state->allActions()) {
            TreeNode actionLeaf;
            actionLeaf.label = action.label.empty() ? action.name : action.label;
            actionLeaf.id = action.id;
            actionLeaf.type = "action";
            actionLeaf.isLeaf = true;
            actionsNode.children.push_back(actionLeaf);
        }
        roots.push_back(actionsNode);
    }

    // Maps — connected MIDI devices with activity lights
    {
        TreeNode mapsNode;
        mapsNode.label = "Maps";
        mapsNode.type = "category";

        auto midiDevices = juce::MidiInput::getAvailableDevices();
        auto now = juce::Time::currentTimeMillis();

        for (auto& midi : midiDevices) {
            auto portName = midi.name.toStdString();
            auto* device = state->findDeviceByPortName(portName);

            TreeNode leaf;
            if (device) {
                leaf.label = device->name;
                leaf.id = device->id;
                leaf.type = "map_device";
            } else {
                leaf.label = portName;
                leaf.id = portName;
                leaf.type = "map_unregistered";
            }
            leaf.isLeaf = true;

            // Activity indicator: check if this port had recent MIDI activity
            if (coordinator) {
                int64_t lastMs = coordinator->getMidiPortActivityMs(portName);
                bool active = (lastMs > 0 && now - lastMs < 400);
                // Encode activity in the type suffix so RegistryTree can draw it
                if (active) leaf.type += "_active";
            }

            mapsNode.children.push_back(leaf);
        }

        roots.push_back(mapsNode);
    }

    // Devices — Audio and MIDI subsections
    {
        TreeNode devicesNode;
        devicesNode.label = "Devices";
        devicesNode.type = "category";

        // Audio devices — each device is a node with Input/Output children
        if (engineAPI) {
            auto& dm = engineAPI->getDeviceManager();
            auto setup = dm.getAudioDeviceSetup();

            TreeNode audioNode;
            audioNode.label = "Audio";
            audioNode.type = "category";

            if (auto* type = dm.getCurrentDeviceTypeObject()) {
                auto outputNames = type->getDeviceNames(false);
                auto inputNames = type->getDeviceNames(true);

                // Collect unique device names across input and output
                std::set<juce::String> allNames;
                for (auto& n : outputNames) allNames.insert(n);
                for (auto& n : inputNames) allNames.insert(n);

                for (auto& name : allNames) {
                    auto nameStr = name.toStdString();
                    bool hasOutput = outputNames.contains(name);
                    bool hasInput = inputNames.contains(name);

                    TreeNode deviceNode;
                    deviceNode.label = nameStr;
                    deviceNode.id = "audio_both:" + nameStr;
                    deviceNode.type = "audio_device";
                    deviceNode.expanded = true;  // always expanded

                    if (hasInput) {
                        TreeNode inLeaf;
                        inLeaf.label = "Input";
                        bool isActive = (name == setup.inputDeviceName);
                        inLeaf.id = "audio_in:" + nameStr;
                        inLeaf.type = isActive ? "audio_input_active" : "audio_input";
                        inLeaf.isLeaf = true;
                        deviceNode.children.push_back(inLeaf);
                    }
                    if (hasOutput) {
                        TreeNode outLeaf;
                        outLeaf.label = "Output";
                        bool isActive = (name == setup.outputDeviceName);
                        outLeaf.id = "audio_out:" + nameStr;
                        outLeaf.type = isActive ? "audio_output_active" : "audio_output";
                        outLeaf.isLeaf = true;
                        deviceNode.children.push_back(outLeaf);
                    }

                    audioNode.children.push_back(deviceNode);
                }
            }
            // Buffer size / sample rate info
            if (auto* device = dm.getCurrentAudioDevice()) {
                int bufSize = device->getCurrentBufferSizeSamples();
                double sampleRate = device->getCurrentSampleRate();
                float latencyMs = (float)bufSize / (float)sampleRate * 1000.0f;
                char label[64];
                snprintf(label, sizeof(label), "Buffer: %d (%.1fms @ %.0fkHz)",
                         bufSize, latencyMs, sampleRate / 1000.0);
                TreeNode bufLeaf;
                bufLeaf.label = label;
                bufLeaf.id = "audio_buffer";
                bufLeaf.type = "audio_buffer";
                bufLeaf.isLeaf = true;
                audioNode.children.push_back(bufLeaf);
            }

            devicesNode.children.push_back(audioNode);
        }

        roots.push_back(devicesNode);
    }

    // Panes — four slots, each with content options
    {
        TreeNode panesNode;
        panesNode.label = "Panes";
        panesNode.type = "category";

        struct SlotDef {
            const char* label;
            const char* slotKey;
            std::vector<std::string> allowedContent;
        };
        SlotDef slots[] = {
            { "Sidebar", "sidebar", { "hidden", "sidebar_tree" } },
            { "Left Pane", "left", { "hidden", "produce", "mappings", "debug" } },
            { "Right Pane", "right", { "hidden", "chat", "logs" } },
            { "Bottom Pane", "bottom", { "hidden", "mixer" } }
        };

        // Content display names
        auto contentLabel = [](const std::string& key) -> std::string {
            if (key == "hidden") return "Hide";
            if (key == "sidebar_tree") return "Sidebar";
            if (key == "produce") return "Produce";
            if (key == "mappings") return "Mappings";
            if (key == "debug") return "Debug";
            if (key == "chat") return "Chat";
            if (key == "logs") return "Logs";
            if (key == "mixer") return "Mixer";
            return key;
        };

        // Collect all currently assigned content (for exclusivity)
        std::set<std::string> assignedContent;
        for (auto& slot : slots) {
            if (getPaneContent) {
                auto c = getPaneContent(slot.slotKey);
                if (c != "hidden") assignedContent.insert(c);
            }
        }

        for (auto& slot : slots) {
            TreeNode slotNode;
            slotNode.label = slot.label;
            slotNode.type = "pane_slot";
            slotNode.id = slot.slotKey;

            std::string currentContent;
            if (getPaneContent)
                currentContent = getPaneContent(slot.slotKey);

            for (auto& contentKey : slot.allowedContent) {
                TreeNode leaf;
                leaf.label = contentLabel(contentKey);
                leaf.id = std::string(slot.slotKey) + ":" + contentKey;
                leaf.type = "pane_content";
                leaf.isLeaf = true;
                leaf.active = (currentContent == contentKey);
                leaf.indent = 1;
                // Disable if this content is assigned to another slot
                if (contentKey != "hidden" && contentKey != currentContent
                    && assignedContent.count(contentKey))
                    leaf.type = "pane_content_disabled";
                slotNode.children.push_back(leaf);
            }

            panesNode.children.push_back(slotNode);
        }

        roots.push_back(panesNode);
    }

    tree.setRootNodes(std::move(roots));
}
