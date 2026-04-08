#include "gui/LogPane.h"

LogPane::LogPane() {}

LogPane::~LogPane() {
    deactivate();
}

void LogPane::activate() {
    if (active) return;
    active = true;
    if (logFile) fclose(logFile);
    logFile = fopen("/tmp/performance.log", "r");
    if (logFile) {
        // Read existing content
        char buf[4096];
        while (fgets(buf, sizeof(buf), logFile)) {
            std::string line(buf);
            if (!line.empty() && line.back() == '\n')
                line.pop_back();
            lines.push_back(std::move(line));
            while ((int)lines.size() > maxLines)
                lines.pop_front();
        }
        // Position at end
        int visibleLines = getHeight() / lineHeight;
        scrollOffset = std::max(0, (int)lines.size() - visibleLines);
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
    clearerr(logFile);  // reset EOF flag so fgets can read new data
    bool changed = false;
    char buf[4096];
    while (fgets(buf, sizeof(buf), logFile)) {
        std::string line(buf);
        if (!line.empty() && line.back() == '\n')
            line.pop_back();
        lines.push_back(std::move(line));
        while ((int)lines.size() > maxLines)
            lines.pop_front();
        changed = true;
    }
    if (changed) {
        if (autoScroll) {
            int visibleLines = getHeight() / lineHeight;
            scrollOffset = std::max(0, (int)lines.size() - visibleLines);
        }
        repaint();
    }
}

void LogPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgPanel));

    if (!active || lines.empty()) {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(12.0f));
        g.drawText(active ? "No log output" : "Log pane inactive",
                   getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    int y = 2;
    int visibleLines = getHeight() / lineHeight;
    int endIdx = std::min(scrollOffset + visibleLines, (int)lines.size());

    for (int i = scrollOffset; i < endIdx; ++i) {
        auto& line = lines[i];

        // Color based on content
        juce::Colour textCol = Theme::color(Theme::Color::textSecondary);
        if (line.find("ERROR") != std::string::npos || line.find("FAILED") != std::string::npos)
            textCol = juce::Colour(0xffff4444);
        else if (line.find("[Engine]") != std::string::npos)
            textCol = juce::Colour(0xff88aacc);
        else if (line.find("[Coordinator]") != std::string::npos)
            textCol = juce::Colour(0xff88cc88);
        else if (line.find("[MIDI]") != std::string::npos)
            textCol = juce::Colour(0xffccaa66);
        else if (line.find("[EngineSync]") != std::string::npos)
            textCol = juce::Colour(0xffaa88cc);
        else if (line.find("[Persistence]") != std::string::npos)
            textCol = juce::Colour(0xff66aaaa);

        g.setColour(textCol);
        g.drawText(juce::String(line), 8, y, getWidth() - 16, lineHeight,
                   juce::Justification::centredLeft, true);
        y += lineHeight;
    }
}

void LogPane::resized() {}

void LogPane::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    int visibleLines = getHeight() / lineHeight;
    int maxScroll = std::max(0, (int)lines.size() - visibleLines);
    scrollOffset = juce::jlimit(0, maxScroll, scrollOffset - (int)(wheel.deltaY * 5));
    autoScroll = (scrollOffset >= maxScroll);
    repaint();
}
