#include "gui/RegistryTree.h"

RegistryTree::RegistryTree() {}

static void applyExpansion(std::vector<TreeNode>& nodes, const std::set<std::string>& expanded,
                            const std::string& prefix = "") {
    for (auto& node : nodes) {
        auto key = prefix + "/" + node.label;
        node.expanded = expanded.count(key) > 0;
        applyExpansion(node.children, expanded, key);
    }
}

void RegistryTree::setRootNodes(std::vector<TreeNode> nodes) {
    roots = std::move(nodes);
    applyExpansion(roots, expandedKeys);
    buildVisibleRows();
    repaint();
}

void RegistryTree::buildVisibleRows() {
    visibleRows.clear();
    for (auto& root : roots)
        buildRowsRecursive(root, 0, "");
}

void RegistryTree::buildRowsRecursive(TreeNode& node, int depth, const std::string& parentKey) {
    auto key = parentKey + "/" + node.label;
    int y = (int)visibleRows.size() * rowHeight - scrollOffset;
    visibleRows.push_back({ &node, depth, y, key });

    if (node.expanded) {
        for (auto& child : node.children)
            buildRowsRecursive(child, depth + 1, key);
    }
}

void RegistryTree::drawArrow(juce::Graphics& g, int x, int y, bool expanded) {
    juce::Path arrow;
    auto area = juce::Rectangle<float>((float)x, (float)y + 5.0f, 10.0f, 10.0f);
    if (expanded) {
        arrow.addTriangle(area.getX(), area.getY(),
                          area.getRight(), area.getY(),
                          area.getCentreX(), area.getBottom());
    } else {
        arrow.addTriangle(area.getX(), area.getY(),
                          area.getX(), area.getBottom(),
                          area.getRight(), area.getCentreY());
    }
    g.fillPath(arrow);
}

void RegistryTree::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a1a));

    auto font = juce::Font(juce::FontOptions(12.0f));
    g.setFont(font);

    for (int i = 0; i < (int)visibleRows.size(); ++i) {
        auto& row = visibleRows[i];
        int y = row.y;

        if (y + rowHeight < 0 || y > getHeight()) continue;

        int x = 8 + row.depth * indentSize;

        // Hover highlight
        if (i == hoveredRow) {
            g.setColour(juce::Colour(0xff2a2a2a));
            g.fillRect(0, y, getWidth(), rowHeight);
        }

        if (row.node->isLeaf) {
            // Leaf: just the label, slightly indented past where arrow would be
            g.setColour(juce::Colour(0xffaaaaaa));
            g.drawText(juce::String(row.node->label),
                       x + 14, y, getWidth() - x - 20, rowHeight,
                       juce::Justification::centredLeft);
        } else {
            // Category: arrow + label
            g.setColour(juce::Colour(0xff888888));
            drawArrow(g, x, y, row.node->expanded);

            g.setColour(juce::Colour(0xffdddddd));
            g.drawText(juce::String(row.node->label),
                       x + 14, y, getWidth() - x - 20, rowHeight,
                       juce::Justification::centredLeft);
        }
    }
}

void RegistryTree::mouseUp(const juce::MouseEvent& event) {
    for (int i = 0; i < (int)visibleRows.size(); ++i) {
        auto& row = visibleRows[i];
        auto bounds = juce::Rectangle<int>(0, row.y, getWidth(), rowHeight);
        if (bounds.contains(event.getPosition())) {
            if (row.node->isLeaf) {
                if (onLeafClick)
                    onLeafClick(row.node->type, row.node->id, row.node->label);
            } else {
                row.node->expanded = !row.node->expanded;
                if (row.node->expanded)
                    expandedKeys.insert(row.key);
                else
                    expandedKeys.erase(row.key);
                buildVisibleRows();
                repaint();
            }
            break;
        }
    }
}

void RegistryTree::mouseMove(const juce::MouseEvent& event) {
    int newHovered = -1;
    for (int i = 0; i < (int)visibleRows.size(); ++i) {
        auto bounds = juce::Rectangle<int>(0, visibleRows[i].y, getWidth(), rowHeight);
        if (bounds.contains(event.getPosition())) {
            newHovered = i;
            break;
        }
    }
    if (newHovered != hoveredRow) {
        hoveredRow = newHovered;
        repaint();
    }
}

void RegistryTree::mouseExit(const juce::MouseEvent&) {
    if (hoveredRow != -1) {
        hoveredRow = -1;
        repaint();
    }
}

void RegistryTree::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    scrollOffset -= (int)(wheel.deltaY * 60);
    scrollOffset = std::max(0, scrollOffset);
    int maxScroll = std::max(0, (int)visibleRows.size() * rowHeight - getHeight());
    scrollOffset = std::min(scrollOffset, maxScroll);
    buildVisibleRows();
    repaint();
}
