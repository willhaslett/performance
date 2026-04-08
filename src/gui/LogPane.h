#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include <deque>
#include <string>

class LogPane : public juce::Component, private juce::Timer {
public:
    LogPane();
    ~LogPane() override;

    void activate();
    void deactivate();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

private:
    void timerCallback() override;

    FILE* logFile = nullptr;
    std::deque<std::string> lines;
    static constexpr int maxLines = 2000;
    static constexpr int lineHeight = 14;
    int scrollOffset = 0;
    bool autoScroll = true;
    bool active = false;
};
