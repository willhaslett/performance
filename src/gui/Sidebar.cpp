#include "gui/Sidebar.h"
#include "api/StateAPI.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

Sidebar::Sidebar() {
    addAndMakeVisible(tree);
    startTimerHz(4);  // check active song highlight

    tree.setOnNodeClick([this](const std::string& type, const std::string& id, const std::string& label) {
        if (!state) return;
        perfLog("[Sidebar] Clicked %s: %s (%s)\n", type.c_str(), label.c_str(), id.c_str());

        if (type == "song" && onLoadSong)
            onLoadSong(id);
    });
}

Sidebar::~Sidebar() {
    if (state && subscriptionId >= 0)
        state->events().unsubscribe(subscriptionId);
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
    auto song = state->currentSong();
    std::string currentId = song ? song->id : "";
    if (currentId != lastHighlightedId) {
        lastHighlightedId = currentId;
        tree.setHighlightedId(currentId);
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

    tree.setRootNodes(std::move(roots));
}
