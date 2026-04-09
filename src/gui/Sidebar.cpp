#include "gui/Sidebar.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

Sidebar::Sidebar() {
    addAndMakeVisible(tree);
    startTimerHz(4);  // check active song highlight

    tree.setOnNodeClick([this](const std::string& type, const std::string& id, const std::string& label) {
        if (!state) return;
        perfLog("[Sidebar] Clicked %s: %s (%s)\n", type.c_str(), label.c_str(), id.c_str());

        if (type == "song" && onLoadSong) {
            selectedDeviceId.clear();
            onLoadSong(id);
        }
        else if (type == "device" && onDeviceSelected) {
            selectedDeviceId = id;
            onDeviceSelected(id, "");
        } else if (type == "unregistered_device" && onDeviceSelected) {
            selectedDeviceId = id;
            onDeviceSelected("", id);  // id is the port name for unregistered devices
        } else if (type == "audio_device") {
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
        } else if (type == "bindings") {
            selectedDeviceId = id;
            if (onBindingsSelected) onBindingsSelected();
        } else if (type == "debug") {
            selectedDeviceId = id;
            if (onDebugSelected) onDebugSelected();
        } else if (type == "logs") {
            selectedDeviceId = id;
            if (onLogsSelected) onLogsSelected();
        } else if (type == "chat") {
            selectedDeviceId = id;
            if (onChatSelected) onChatSelected();
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

    // Poll for MIDI device changes
    auto devices = juce::MidiInput::getAvailableDevices();
    int count = (int)devices.size();
    if (count != lastMidiDeviceCount) {
        lastMidiDeviceCount = count;
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
            devicesNode.children.push_back(audioNode);
        }

        // MIDI devices
        {
            TreeNode midiNode;
            midiNode.label = "MIDI";
            midiNode.type = "category";

            auto midiDevices = juce::MidiInput::getAvailableDevices();
            for (auto& midi : midiDevices) {
                auto portName = midi.name.toStdString();
                auto* device = state->findDeviceByPortName(portName);

                TreeNode deviceLeaf;
                if (device) {
                    deviceLeaf.label = device->name;
                    deviceLeaf.id = device->id;
                    deviceLeaf.type = "device";
                } else {
                    deviceLeaf.label = portName;
                    deviceLeaf.id = portName;
                    deviceLeaf.type = "unregistered_device";
                }
                deviceLeaf.isLeaf = true;
                midiNode.children.push_back(deviceLeaf);
            }

            devicesNode.children.push_back(midiNode);
        }

        roots.push_back(devicesNode);
    }

    // Panes
    {
        TreeNode panesNode;
        panesNode.label = "Panes";
        panesNode.type = "category";

        auto addLeaf = [&](const char* label, const char* id, const char* type) {
            TreeNode leaf;
            leaf.label = label;
            leaf.id = id;
            leaf.type = type;
            leaf.isLeaf = true;
            panesNode.children.push_back(leaf);
        };

        addLeaf("Bindings", "bindings", "bindings");
        addLeaf("Debug", "debug", "debug");
        addLeaf("Logs", "logs", "logs");
        addLeaf("Chat", "chat", "chat");

        roots.push_back(panesNode);
    }

    tree.setRootNodes(std::move(roots));
}
