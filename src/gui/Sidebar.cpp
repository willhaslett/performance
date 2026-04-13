#include "gui/Sidebar.h"
#include <set>
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

Sidebar::Sidebar() {
    addAndMakeVisible(tree);
    startTimerHz(4);

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
            auto devName = id.substr(id.find(':') + 1);
            if (onAudioOutputSelected) onAudioOutputSelected(devName);
            if (onAudioInputSelected) onAudioInputSelected(devName);
        } else if (type == "audio_output" || type == "audio_output_active") {
            selectedDeviceId = id;
            auto devName = id.substr(id.find(':') + 1);
            if (onAudioOutputSelected) onAudioOutputSelected(devName);
        } else if (type == "audio_input" || type == "audio_input_active") {
            selectedDeviceId = id;
            auto devName = id.substr(id.find(':') + 1);
            if (onAudioInputSelected) onAudioInputSelected(devName);
        } else if (type == "map_device" || type == "map_device_active") {
            selectedDeviceId = id;
            auto* device = state->findDevice(id);
            if (device && onMapSelected) {
                onMapSelected(device->id, device->midiPortName);
            }
        } else if (type == "map_unregistered" || type == "map_unregistered_active") {
            selectedDeviceId = id;
            if (onMapSelected) {
                juce::PopupMenu menu;
                menu.addItem(1, "Register Device");
                auto portName = id;
                menu.showMenuAsync(juce::PopupMenu::Options(),
                    [this, portName](int result) {
                        if (result == 1 && state) {
                            auto devId = state->registerDevice(portName, portName);
                            if (onMapSelected)
                                onMapSelected(devId, portName);
                            refreshTree();
                        }
                    });
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

    subscriptionId = state->events().subscribe([this](const StateEvent&) {
        needsRefresh = true;
        juce::MessageManager::callAsync([this] {
            if (needsRefresh) {
                needsRefresh = false;
                refreshTree();
            }
        });
    });

    refreshTree();
}

// --- Tab bar ---

juce::Rectangle<int> Sidebar::getTabBounds(int tabIndex) const {
    int w = getWidth() / tabCount;
    return { tabIndex * w, 0, w, tabBarHeight };
}

void Sidebar::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgPanel));

    // Tab bar
    static const char* tabIcons[] = {
        "\xf0\x9f\x93\x81",  // Songs (folder)
        "\xf0\x9f\x8e\xb9",  // Library (keyboard)
        "\xe2\x9a\xa1",       // Actions (lightning)
        "\xf0\x9f\x94\xa7"   // Devices (wrench)
    };
    static const char* tabLabels[] = { "Songs", "Library", "Actions", "Devices" };

    for (int i = 0; i < tabCount; ++i) {
        auto bounds = getTabBounds(i);
        bool active = (i == (int)activeTab);

        if (active) {
            g.setColour(Theme::color(Theme::Color::bgApp));
            g.fillRect(bounds);
        }

        // Bottom border on inactive tabs
        if (!active) {
            g.setColour(Theme::color(Theme::Color::border));
            g.drawLine((float)bounds.getX(), (float)bounds.getBottom() - 1,
                       (float)bounds.getRight(), (float)bounds.getBottom() - 1, 1.0f);
        }

        // Label
        g.setColour(active ? Theme::color(Theme::Color::textPrimary)
                            : Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(10.0f));
        g.drawText(tabLabels[i], bounds, juce::Justification::centred);
    }

    // Right border
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)getWidth(), 0.0f, (float)getWidth(), (float)getHeight(), 1.0f);
}

void Sidebar::mouseUp(const juce::MouseEvent& event) {
    if (event.getPosition().getY() < tabBarHeight) {
        for (int i = 0; i < tabCount; ++i) {
            if (getTabBounds(i).contains(event.getPosition())) {
                activeTab = (Tab)i;
                refreshTree();
                repaint();
                return;
            }
        }
    }
}

void Sidebar::resized() {
    tree.setBounds(0, tabBarHeight, getWidth(), getHeight() - tabBarHeight);
}

void Sidebar::timerCallback() {
    if (!state) return;

    std::string highlightId = selectedDeviceId;
    if (highlightId.empty()) {
        auto song = state->currentSong();
        highlightId = song ? song->id : "";
    }
    if (highlightId != lastHighlightedId) {
        lastHighlightedId = highlightId;
        tree.setHighlightedId(highlightId);
    }

    // Poll MIDI device changes
    auto devices = juce::MidiInput::getAvailableDevices();
    int count = (int)devices.size();
    if (count != lastMidiDeviceCount) {
        lastMidiDeviceCount = count;
        if (coordinator) coordinator->refreshMidiDevices();
        refreshTree();
    }

    // Poll audio device changes
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
    switch (activeTab) {
        case Songs:   refreshSongsTab(); break;
        case Library: refreshLibraryTab(); break;
        case Actions: refreshActionsTab(); break;
        case Devices: refreshDevicesTab(); break;
        default: break;
    }
}

void Sidebar::refreshSongsTab() {
    if (!state) return;
    std::vector<TreeNode> roots;

    // Sandbox first
    for (auto& song : state->allSongs()) {
        if (song.name == "Sandbox") {
            TreeNode n;
            n.label = "Sandbox";
            n.id = song.id;
            n.type = "song";
            n.isLeaf = true;
            roots.push_back(n);
            break;
        }
    }
    // User songs
    for (auto& song : state->allSongs()) {
        if (song.name == "Sandbox") continue;
        TreeNode n;
        n.label = song.name;
        n.id = song.id;
        n.type = "song";
        n.isLeaf = true;
        roots.push_back(n);
    }

    tree.setRootNodes(std::move(roots));
}

void Sidebar::refreshLibraryTab() {
    if (!state) return;
    std::vector<TreeNode> roots;

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
            TreeNode leaf;
            leaf.label = preset->name;
            leaf.id = preset->id;
            leaf.type = "preset";
            leaf.isLeaf = true;
            pluginNode.children.push_back(leaf);
        }

        if (plugin.isInstrument)
            instrumentsNode.children.push_back(pluginNode);
        else
            effectsNode.children.push_back(pluginNode);
    }

    if (!instrumentsNode.children.empty())
        roots.push_back(instrumentsNode);
    if (!effectsNode.children.empty())
        roots.push_back(effectsNode);

    tree.setRootNodes(std::move(roots));
}

void Sidebar::refreshActionsTab() {
    if (!state) return;
    std::vector<TreeNode> roots;

    for (auto& action : state->allActions()) {
        TreeNode leaf;
        leaf.label = action.label.empty() ? action.name : action.label;
        leaf.id = action.id;
        leaf.type = "action";
        leaf.isLeaf = true;
        roots.push_back(leaf);
    }

    tree.setRootNodes(std::move(roots));
}

void Sidebar::refreshDevicesTab() {
    if (!state) return;
    std::vector<TreeNode> roots;

    // MIDI devices (Maps)
    {
        TreeNode midiNode;
        midiNode.label = "MIDI";
        midiNode.type = "category";

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

            if (coordinator) {
                int64_t lastMs = coordinator->getMidiPortActivityMs(portName);
                bool active = (lastMs > 0 && now - lastMs < 400);
                if (active) leaf.type += "_active";
            }

            midiNode.children.push_back(leaf);
        }

        roots.push_back(midiNode);
    }

    // Audio devices
    if (engineAPI) {
        auto& dm = engineAPI->getDeviceManager();
        auto setup = dm.getAudioDeviceSetup();

        TreeNode audioNode;
        audioNode.label = "Audio";
        audioNode.type = "category";

        if (auto* type = dm.getCurrentDeviceTypeObject()) {
            auto outputNames = type->getDeviceNames(false);
            auto inputNames = type->getDeviceNames(true);

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
                deviceNode.expanded = true;

                if (hasInput) {
                    TreeNode inLeaf;
                    inLeaf.label = "Input";
                    inLeaf.id = "audio_in:" + nameStr;
                    inLeaf.type = (name == setup.inputDeviceName) ? "audio_input_active" : "audio_input";
                    inLeaf.isLeaf = true;
                    deviceNode.children.push_back(inLeaf);
                }
                if (hasOutput) {
                    TreeNode outLeaf;
                    outLeaf.label = "Output";
                    outLeaf.id = "audio_out:" + nameStr;
                    outLeaf.type = (name == setup.outputDeviceName) ? "audio_output_active" : "audio_output";
                    outLeaf.isLeaf = true;
                    deviceNode.children.push_back(outLeaf);
                }

                audioNode.children.push_back(deviceNode);
            }
        }

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

        roots.push_back(audioNode);
    }

    tree.setRootNodes(std::move(roots));
}
