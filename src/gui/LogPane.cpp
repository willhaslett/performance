#include "gui/LogPane.h"

LogPane::LogPane() {
    editor.setMultiLine(true, false);
    editor.setReadOnly(true);
    editor.setScrollbarsShown(true);
    editor.setCaretVisible(false);
    editor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    editor.setColour(juce::TextEditor::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    editor.setColour(juce::TextEditor::textColourId, Theme::color(Theme::Color::textSecondary));
    editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour(juce::ScrollBar::thumbColourId, Theme::color(Theme::Color::bgSlotHover));
    addAndMakeVisible(editor);
}

LogPane::~LogPane() {
    deactivate();
}

void LogPane::activate() {
    if (active) return;
    active = true;
    editor.clear();
    lineCount = 0;

    if (logFile) fclose(logFile);
    logFile = fopen("/tmp/performance.log", "r");
    if (logFile) {
        // Read existing content
        juce::String content;
        char buf[4096];
        while (fgets(buf, sizeof(buf), logFile)) {
            content += juce::String::fromUTF8(buf);
            lineCount++;
        }
        editor.setText(content, false);
        editor.moveCaretToEnd();
    }
    startTimerHz(10);
}

void LogPane::deactivate() {
    if (!active) return;
    active = false;
    stopTimer();
    if (logFile) { fclose(logFile); logFile = nullptr; }
}

void LogPane::timerCallback() {
    if (!logFile) return;
    clearerr(logFile);

    juce::String newText;
    char buf[4096];
    bool changed = false;
    while (fgets(buf, sizeof(buf), logFile)) {
        newText += juce::String::fromUTF8(buf);
        lineCount++;
        changed = true;
    }

    if (changed) {
        // Check if caret is at the end (auto-scroll)
        bool atEnd = (editor.getCaretPosition() >= editor.getText().length() - 1);

        editor.setCaretPosition(editor.getText().length());
        editor.insertTextAtCaret(newText);

        if (atEnd)
            editor.moveCaretToEnd();
    }
}

void LogPane::resized() {
    editor.setBounds(getLocalBounds());
}
