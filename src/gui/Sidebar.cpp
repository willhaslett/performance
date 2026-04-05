#include "gui/Sidebar.h"
#include "registry/Registry.h"

Sidebar::Sidebar() {
    addAndMakeVisible(tree);

    tree.setOnLeafClick([](const std::string& type, const std::string& id, const std::string& label) {
        // TODO: handle leaf clicks (open editor, load song, etc.)
    });

    startTimerHz(2);  // refresh tree periodically
}

void Sidebar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a1a));

    // Right border
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawLine((float)getWidth(), 0.0f, (float)getWidth(), (float)getHeight(), 1.0f);
}

void Sidebar::resized() {
    tree.setBounds(getLocalBounds().reduced(0, 4));
}

void Sidebar::timerCallback() {
    if (registry)
        refreshTree();
}

void Sidebar::refreshTree() {
    std::vector<TreeNode> roots;

    // Songs
    {
        TreeNode songsNode;
        songsNode.label = "Songs";
        songsNode.type = "category";
        for (auto& song : registry->allSongs()) {
            TreeNode songNode;
            songNode.label = song.name;
            songNode.id = song.id;
            songNode.type = "song";

            // Tracks under this song
            for (auto& track : registry->tracksForSong(song.id)) {
                TreeNode trackNode;
                trackNode.label = track.name;
                trackNode.id = track.id;
                trackNode.type = "track";

                // Plugin
                auto plugin = registry->findPluginById(track.pluginId);
                if (plugin) {
                    TreeNode pluginLeaf;
                    pluginLeaf.label = plugin->name;
                    pluginLeaf.id = plugin->id;
                    pluginLeaf.type = "plugin";
                    pluginLeaf.isLeaf = true;
                    trackNode.children.push_back(pluginLeaf);
                }

                // Effects
                for (auto& fx : registry->effectsForParent(track.id)) {
                    auto fxPlugin = registry->findPluginById(fx.pluginId);
                    TreeNode fxLeaf;
                    fxLeaf.label = fx.name + (fxPlugin ? " (" + fxPlugin->name + ")" : "");
                    fxLeaf.id = fx.id;
                    fxLeaf.type = "effect";
                    fxLeaf.isLeaf = true;
                    trackNode.children.push_back(fxLeaf);
                }

                songNode.children.push_back(trackNode);
            }

            // Busses under this song
            for (auto& bus : registry->bussesForSong(song.id)) {
                TreeNode busNode;
                busNode.label = bus.name + " (bus)";
                busNode.id = bus.id;
                busNode.type = "bus";

                for (auto& fx : registry->effectsForParent(bus.id)) {
                    auto fxPlugin = registry->findPluginById(fx.pluginId);
                    TreeNode fxLeaf;
                    fxLeaf.label = fx.name + (fxPlugin ? " (" + fxPlugin->name + ")" : "");
                    fxLeaf.id = fx.id;
                    fxLeaf.type = "effect";
                    fxLeaf.isLeaf = true;
                    busNode.children.push_back(fxLeaf);
                }

                songNode.children.push_back(busNode);
            }

            songsNode.children.push_back(songNode);
        }
        roots.push_back(songsNode);
    }

    // Snapshots
    {
        TreeNode snapshotsNode;
        snapshotsNode.label = "Snapshots";
        snapshotsNode.type = "category";

        // Group by plugin
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

            snapshotsNode.children.push_back(pluginNode);
        }
        if (!snapshotsNode.children.empty())
            roots.push_back(snapshotsNode);
    }

    // Actions
    {
        TreeNode actionsNode;
        actionsNode.label = "Actions";
        actionsNode.type = "category";

        for (auto& action : registry->allActions()) {
            TreeNode actionLeaf;
            actionLeaf.label = action.name;
            actionLeaf.id = action.id;
            actionLeaf.type = "action";
            actionLeaf.isLeaf = true;
            actionsNode.children.push_back(actionLeaf);
        }
        roots.push_back(actionsNode);
    }

    tree.setRootNodes(std::move(roots));
}
