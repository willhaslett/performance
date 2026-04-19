#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "gui/Theme.h"
#include <string>

class LogPane : public juce::Component, private juce::Timer {
public:
    LogPane();
    ~LogPane() override;

    void activate();
    void deactivate();

    void resized() override;

private:
    void timerCallback() override;
    void exportLogs();

    juce::CodeDocument document;
    juce::CodeEditorComponent codeEditor;
    juce::TextButton exportButton { "Export Logs" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    FILE* logFile = nullptr;
    bool active = false;
    int lineCount = 0;
    static constexpr int maxLines = 5000;
};
