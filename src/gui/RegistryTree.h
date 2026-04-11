#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <string>
#include <set>

// A collapsible tree node with children
struct TreeNode {
    std::string label;
    std::string id;        // registry entity ID (empty for category nodes)
    std::string type;      // entity type: "song", "track", "plugin", "preset", etc.
    bool expanded = false;
    bool isLeaf = false;
    bool active = false;   // green dot indicator (e.g., active pane)
    int indent = 0;        // extra indentation level
    std::vector<TreeNode> children;
};

class RegistryTree : public juce::Component {
public:
    using NodeClickCallback = std::function<void(const std::string& type, const std::string& id,
                                                  const std::string& label)>;

    RegistryTree();

    void setRootNodes(std::vector<TreeNode> nodes);
    void setOnNodeClick(NodeClickCallback cb) { onNodeClick = std::move(cb); }
    void setOnNodeDoubleClick(NodeClickCallback cb) { onNodeDoubleClick = std::move(cb); }
    void setHighlightedId(const std::string& id) { highlightedId = id; repaint(); }

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

private:
    std::vector<TreeNode> roots;
    NodeClickCallback onNodeClick;
    NodeClickCallback onNodeDoubleClick;
    std::string highlightedId;
    int hoveredRow = -1;
    int scrollOffset = 0;
    std::set<std::string> expandedKeys;  // persists across rebuilds

    static constexpr int rowHeight = 22;
    static constexpr int indentSize = 16;

    struct RowInfo {
        std::string label;
        std::string id;
        std::string type;
        std::string key;
        bool isLeaf;
        bool expanded;
        bool active = false;
        int indent = 0;
        int depth;
        int y;
    };
    std::vector<RowInfo> visibleRows;

    void buildVisibleRows();
    void buildRowsRecursive(TreeNode& node, int depth, const std::string& parentKey);
    void drawArrow(juce::Graphics& g, int x, int y, bool expanded);
};
