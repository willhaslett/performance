#include "gui/KeyBindingEditor.h"

KeyBindingEditor::KeyBindingEditor(KeyBindingManager& mgr) : manager(mgr) {
    setWantsKeyboardFocus(true);
    addAndMakeVisible(closeButton);
    addAndMakeVisible(restoreButton);
    closeButton.onClick = [this]() { if (onClose) onClose(); };
    restoreButton.onClick = [this]() {
        manager.restoreAllDefaults();
        repaint();
    };
    buildRows();
}

void KeyBindingEditor::buildRows() {
    rows.clear();
    std::string lastCat;
    for (int i = 0; i < (int)manager.allCommands().size(); ++i) {
        auto& cmd = manager.allCommands()[i];
        if (cmd.category != lastCat) {
            Row r;
            r.isCategory = true;
            r.categoryName = cmd.category;
            rows.push_back(r);
            lastCat = cmd.category;
        }
        Row r;
        r.commandIndex = i;
        rows.push_back(r);
    }
}

void KeyBindingEditor::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    // Title
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(16.0f));
    g.drawText("Keyboard Shortcuts", 0, 8, getWidth(), 28, juce::Justification::centred);

    int y = headerHeight - scrollOffset;
    auto& cmds = manager.allCommands();
    int shortcutX = getWidth() - shortcutColWidth - 20;

    for (int ri = 0; ri < (int)rows.size(); ++ri) {
        auto& row = rows[ri];
        if (y + (row.isCategory ? categoryRowHeight : rowHeight) < 0) {
            y += row.isCategory ? categoryRowHeight : rowHeight;
            continue;
        }
        if (y > getHeight()) break;

        if (row.isCategory) {
            g.setColour(Theme::color(Theme::Color::textSecondary));
            g.setFont(Theme::font(13.0f));
            g.drawText(juce::String(row.categoryName), leftMargin, y + 10, 300, 18,
                       juce::Justification::centredLeft);
            // Subtle line under category
            g.setColour(Theme::color(Theme::Color::border));
            g.drawLine((float)leftMargin, (float)(y + categoryRowHeight - 1),
                       (float)(getWidth() - 20), (float)(y + categoryRowHeight - 1), 0.5f);
            y += categoryRowHeight;
        } else {
            auto& cmd = cmds[row.commandIndex];
            bool isCapturing = (capturingIndex == row.commandIndex);

            // Command label
            g.setColour(Theme::color(Theme::Color::textPrimary));
            g.setFont(Theme::font(Theme::fontSizeSm));
            g.drawText(juce::String(cmd.label), leftMargin + 16, y, shortcutX - leftMargin - 20, rowHeight,
                       juce::Justification::centredLeft);

            // Shortcut field
            auto fieldBounds = juce::Rectangle<int>(shortcutX, y + 3, shortcutColWidth, rowHeight - 6);
            if (isCapturing) {
                g.setColour(Theme::color(Theme::Color::accent).withAlpha(0.3f));
                g.fillRoundedRectangle(fieldBounds.toFloat(), 4.0f);
                g.setColour(Theme::color(Theme::Color::accent));
                g.drawRoundedRectangle(fieldBounds.toFloat(), 4.0f, 1.5f);
                g.setFont(Theme::font(11.0f));
                g.setColour(Theme::color(Theme::Color::textSecondary));
                g.drawText("Press a key...", fieldBounds, juce::Justification::centred);
            } else {
                g.setColour(juce::Colour(0xff2a2a2a));
                g.fillRoundedRectangle(fieldBounds.toFloat(), 4.0f);
                g.setColour(Theme::color(Theme::Color::border));
                g.drawRoundedRectangle(fieldBounds.toFloat(), 4.0f, 0.5f);

                auto keyText = KeyBindingManager::keyToString(cmd.currentKey);
                bool isDefault = (cmd.currentKey == cmd.defaultKey);
                g.setColour(isDefault ? Theme::color(Theme::Color::textSecondary)
                                       : Theme::color(Theme::Color::textPrimary));
                g.setFont(Theme::font(12.0f));
                g.drawText(keyText, fieldBounds, juce::Justification::centred);
            }

            y += rowHeight;
        }
    }
}

void KeyBindingEditor::resized() {
    int btnW = 100;
    closeButton.setBounds(getWidth() - btnW - 16, 8, btnW, 28);
    restoreButton.setBounds(16, 8, 130, 28);
}

void KeyBindingEditor::mouseUp(const juce::MouseEvent& event) {
    int y = headerHeight - scrollOffset;

    for (int ri = 0; ri < (int)rows.size(); ++ri) {
        auto& row = rows[ri];
        int h = row.isCategory ? categoryRowHeight : rowHeight;
        if (!row.isCategory && event.getPosition().getY() >= y && event.getPosition().getY() < y + h) {
            int shortcutX = getWidth() - shortcutColWidth - 20;
            if (event.getPosition().getX() >= shortcutX) {
                capturingIndex = row.commandIndex;
                grabKeyboardFocus();
                repaint();
                return;
            }
        }
        y += h;
    }
    capturingIndex = -1;
    repaint();
}

bool KeyBindingEditor::keyPressed(const juce::KeyPress& key) {
    if (capturingIndex >= 0) {
        if (key == juce::KeyPress::escapeKey) {
            capturingIndex = -1;
        } else {
            // Check for conflicts
            auto existingCmd = manager.getCommandForKey(key);
            auto& cmds = manager.allCommands();
            if (!existingCmd.empty() && existingCmd != cmds[capturingIndex].id) {
                // Clear the conflicting binding
                manager.setBinding(existingCmd, juce::KeyPress());
            }
            manager.setBinding(cmds[capturingIndex].id, key);
            capturingIndex = -1;
        }
        repaint();
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        if (onClose) onClose();
        return true;
    }
    return false;
}

void KeyBindingEditor::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    scrollOffset = std::max(0, scrollOffset - (int)(wheel.deltaY * 200));
    repaint();
}
