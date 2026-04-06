#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include <functional>

// Reusable inline text editor overlay.
// Call show() with bounds and initial text. Commits on Enter/focus loss, cancels on Escape.
class InlineEditor : public juce::TextEditor {
public:
    std::function<void(const juce::String& newText)> onCommit;
    std::function<void()> onCancel;

    InlineEditor() {
        setFont(Theme::font(Theme::fontSize));
        setColour(juce::TextEditor::backgroundColourId, Theme::color(Theme::Color::bgSlot));
        setColour(juce::TextEditor::textColourId, Theme::color(Theme::Color::textWhite));
        setColour(juce::TextEditor::outlineColourId, Theme::color(Theme::Color::accent));
        setColour(juce::TextEditor::focusedOutlineColourId, Theme::color(Theme::Color::accent));
        setJustification(juce::Justification::centredLeft);
        setSelectAllWhenFocused(true);
    }

    void show(juce::Component& parent, juce::Rectangle<int> bounds, const juce::String& text) {
        parent.addAndMakeVisible(this);
        setBounds(bounds);
        setText(text, false);
        grabKeyboardFocus();
        selectAll();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey) {
            commit();
            return true;
        }
        if (key == juce::KeyPress::escapeKey) {
            cancel();
            return true;
        }
        return juce::TextEditor::keyPressed(key);
    }

    void focusLost(FocusChangeType) override {
        commit();
    }

private:
    void commit() {
        auto text = getText().trim();
        auto* parent = getParentComponent();
        if (parent) parent->removeChildComponent(this);
        if (onCommit && text.isNotEmpty()) onCommit(text);
    }

    void cancel() {
        auto* parent = getParentComponent();
        if (parent) parent->removeChildComponent(this);
        if (onCancel) onCancel();
    }
};
