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
    std::function<void(const juce::String& newText)> onCommitNext;  // down arrow
    std::function<void(const juce::String& newText)> onCommitPrev;  // up arrow

    InlineEditor() {
        setFont(Theme::font(Theme::fontSize));
        setColour(juce::TextEditor::backgroundColourId, Theme::color(Theme::Color::bgSurface));
        setColour(juce::TextEditor::textColourId, Theme::color(Theme::Color::textOnColor));
        setColour(juce::TextEditor::outlineColourId, Theme::color(Theme::Color::accent));
        setColour(juce::TextEditor::focusedOutlineColourId, Theme::color(Theme::Color::accent));
        setJustification(juce::Justification::centredLeft);
        setSelectAllWhenFocused(true);
    }

private:
    bool isCancelling = false;
    bool isCommitting = false;
public:

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
        if (key == juce::KeyPress::downKey && onCommitNext) {
            commitNav(onCommitNext);
            return true;
        }
        if (key == juce::KeyPress::upKey && onCommitPrev) {
            commitNav(onCommitPrev);
            return true;
        }
        return juce::TextEditor::keyPressed(key);
    }

    void focusLost(FocusChangeType) override {
        if (!isCancelling && !isCommitting)
            commit();
    }

    void commit() {
        if (isCommitting) return;
        isCommitting = true;
        auto text = getText().trim();
        auto* parent = getParentComponent();
        if (parent) parent->removeChildComponent(this);
        isCommitting = false;
        if (onCommit && text.isNotEmpty()) onCommit(text);
    }

    void commitNav(std::function<void(const juce::String&)>& navCallback) {
        isCancelling = true;  // prevent focusLost from double-committing
        auto text = getText().trim();
        auto* parent = getParentComponent();
        if (parent) parent->removeChildComponent(this);
        isCancelling = false;
        if (navCallback && text.isNotEmpty()) navCallback(text);
    }

    void cancel() {
        isCancelling = true;
        auto* parent = getParentComponent();
        if (parent) parent->removeChildComponent(this);
        isCancelling = false;
        if (onCancel) onCancel();
    }
};
