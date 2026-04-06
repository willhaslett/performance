#include "gui/Sidebar.h"
#include "api/PerformanceAPI.h"
#include "registry/Registry.h"
#include "registry/RegistryEvents.h"
#include "engine/Log.h"

Sidebar::Sidebar() {
    addAndMakeVisible(tree);

    tree.setOnNodeClick([this](const std::string& type, const std::string& id, const std::string& label) {
        if (!api) return;
        perfLog("[Sidebar] Clicked %s: %s (%s)\n", type.c_str(), label.c_str(), id.c_str());

        if (type == EntityType::Song)
            api->loadSongFromRegistry(id);
    });
}

Sidebar::~Sidebar() {
    if (registry && subscriptionId >= 0)
        registry->events().unsubscribe(subscriptionId);
}

void Sidebar::setRegistry(Registry* reg) {
    if (registry && subscriptionId >= 0)
        registry->events().unsubscribe(subscriptionId);

    registry = reg;
    if (!registry) return;

    // Subscribe to all registry changes
    subscriptionId = registry->events().subscribe([this](const RegistryEvent&) {
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

void Sidebar::refreshTree() {
    std::vector<TreeNode> roots;

    // Songs — just names, no children (mixer shows contents)
    {
        TreeNode songsNode;
        songsNode.label = "Songs";
        songsNode.type = "category";
        for (auto& song : registry->allSongs()) {
            if (song.name == "Default Session") continue;
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

        // Instruments (plugins that are instruments with saved snapshots)
        TreeNode instrumentsNode;
        instrumentsNode.label = "Instruments";
        instrumentsNode.type = "category";

        TreeNode effectsNode;
        effectsNode.label = "Effects";
        effectsNode.type = "category";

        for (auto& plugin : registry->allPlugins()) {
            auto snaps = registry->snapshotsForPlugin(plugin.id);
            if (snaps.empty()) continue;

            TreeNode pluginNode;
            pluginNode.label = plugin.name;
            pluginNode.type = "plugin";
            pluginNode.id = plugin.id;

            for (auto& snap : snaps) {
                TreeNode snapLeaf;
                snapLeaf.label = snap.name;
                snapLeaf.id = snap.id;
                snapLeaf.type = "snapshot";
                snapLeaf.isLeaf = true;
                pluginNode.children.push_back(snapLeaf);
            }

            // Determine if instrument or effect from plugin description
            auto desc = registry->findPluginById(plugin.id);
            bool isInstrument = false;
            if (desc) {
                // Check format_id for "Synths/" prefix
                isInstrument = desc->formatId.find("Synths/") != std::string::npos;
            }

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

        for (auto& action : registry->allActions()) {
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
