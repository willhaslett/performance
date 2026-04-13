#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/KeyBindingManager.h"

// Full-screen-ish modal for viewing and editing keyboard shortcuts.
// Non-collapsible outline with categories and commands.
// Click a shortcut field to capture a new keypress.

class KeyBindingEditor : public juce::Component {
public:
    KeyBindingEditor(KeyBindingManager& mgr);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void()> onClose;

private:
    KeyBindingManager& manager;

    int scrollOffset = 0;
    int capturingIndex = -1;  // which command is waiting for a keypress (-1 = none)

    static constexpr int rowHeight = 28;
    static constexpr int headerHeight = 44;
    static constexpr int categoryRowHeight = 32;
    static constexpr int shortcutColWidth = 100;
    static constexpr int leftMargin = 24;

    juce::TextButton closeButton { "Done" };
    juce::TextButton restoreButton { "Restore Defaults" };

    // Flat list of rows for rendering (categories + commands interleaved)
    struct Row {
        bool isCategory = false;
        std::string categoryName;
        int commandIndex = -1;  // index into manager.allCommands()
    };
    std::vector<Row> rows;
    void buildRows();

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
};
