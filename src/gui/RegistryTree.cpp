#include "gui/RegistryTree.h"
#include "gui/Theme.h"

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
    visibleRows.clear();
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

    RowInfo row;
    row.label = node.label;
    row.id = node.id;
    row.type = node.type;
    row.key = key;
    row.isLeaf = node.isLeaf;
    row.expanded = node.expanded;
    row.depth = depth;
    row.y = y;
    visibleRows.push_back(row);

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
    g.fillAll(Theme::color(Theme::Color::bgPanel));
    g.setFont(Theme::font(Theme::fontSizeSm));

    for (int i = 0; i < (int)visibleRows.size(); ++i) {
        auto& row = visibleRows[i];
        int y = row.y;

        if (y + rowHeight < 0 || y > getHeight()) continue;

        int x = 8 + row.depth * indentSize;

        bool isActive = !highlightedId.empty() && row.id == highlightedId;

        if (isActive) {
            g.setColour(Theme::color(Theme::Color::bgHeaderOut));
            g.fillRect(0, y, getWidth(), rowHeight);
        } else if (i == hoveredRow) {
            g.setColour(Theme::color(Theme::Color::bgSlot));
            g.fillRect(0, y, getWidth(), rowHeight);
        }

        if (row.isLeaf) {
            // Active audio device: green dot indicator
            if (row.type == "audio_output_active" || row.type == "audio_input_active") {
                g.setColour(Theme::color(Theme::Color::midiActive));
                g.fillEllipse((float)(x + 4), (float)(y + rowHeight / 2 - 3), 6.0f, 6.0f);
            }

            g.setColour(isActive ? Theme::color(Theme::Color::textWhite)
                                 : Theme::color(Theme::Color::textPrimary));
            g.drawText(juce::String(row.label),
                       x + 14, y, getWidth() - x - 20, rowHeight,
                       juce::Justification::centredLeft);
        } else {
            g.setColour(Theme::color(Theme::Color::textSecondary));
            drawArrow(g, x, y, row.expanded);

            g.setColour(Theme::color(Theme::Color::textWhite));
            g.drawText(juce::String(row.label),
                       x + 14, y, getWidth() - x - 20, rowHeight,
                       juce::Justification::centredLeft);
        }
    }
}

void RegistryTree::mouseUp(const juce::MouseEvent& event) {
    std::string clickType, clickId, clickLabel, clickKey;
    bool clickIsLeaf = true;

    for (int i = 0; i < (int)visibleRows.size(); ++i) {
        auto& row = visibleRows[i];
        auto bounds = juce::Rectangle<int>(0, row.y, getWidth(), rowHeight);
        if (bounds.contains(event.getPosition())) {
            clickType = row.type;
            clickId = row.id;
            clickLabel = row.label;
            clickKey = row.key;
            clickIsLeaf = row.isLeaf;

            if (!clickIsLeaf) {
                if (row.expanded)
                    expandedKeys.erase(clickKey);
                else
                    expandedKeys.insert(clickKey);

                // Rebuild from roots with updated expansion
                applyExpansion(roots, expandedKeys);
                buildVisibleRows();
                repaint();
            }
            break;
        }
    }

    // Only dispatch click action for leaf nodes — non-leaf just expands/collapses
    if (clickIsLeaf && !clickId.empty() && onNodeClick)
        onNodeClick(clickType, clickId, clickLabel);
}

void RegistryTree::mouseDoubleClick(const juce::MouseEvent& event) {
    if (!onNodeDoubleClick) return;

    for (int i = 0; i < (int)visibleRows.size(); ++i) {
        auto& row = visibleRows[i];
        auto bounds = juce::Rectangle<int>(0, row.y, getWidth(), rowHeight);
        if (bounds.contains(event.getPosition())) {
            onNodeDoubleClick(row.type, row.id, row.label);
            return;
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
