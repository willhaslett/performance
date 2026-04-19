#include "gui/LogPane.h"

// Simple tokeniser that colors log lines by service tag
class LogTokeniser : public juce::CodeTokeniser {
public:
    enum TokenType { Default, Engine, EngineSync, Coordinator, MIDI, Persistence, Sidebar, IPC, Timestamp };

    int readNextToken(juce::CodeDocument::Iterator& source) override {
        // Read to end of line
        auto lineStart = source.getPosition();
        juce::String line;
        while (!source.isEOF() && source.peekNextChar() != '\n')
            line += source.nextChar();
        if (!source.isEOF()) source.nextChar(); // consume newline

        if (line.contains("[Engine]") && !line.contains("[EngineSync]")) return Engine;
        if (line.contains("[EngineSync]")) return EngineSync;
        if (line.contains("[Coordinator]")) return Coordinator;
        if (line.contains("[MIDI]")) return MIDI;
        if (line.contains("[Persistence]")) return Persistence;
        if (line.contains("[Sidebar]")) return Sidebar;
        if (line.contains("[IPC]")) return IPC;
        return Default;
    }

    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override {
        juce::CodeEditorComponent::ColourScheme scheme;
        scheme.set("Default",      juce::Colour(0xff999999));  // textSecondary
        scheme.set("Engine",       juce::Colour(0xff6a9fcc));  // blue
        scheme.set("EngineSync",   juce::Colour(0xff7aaa8a));  // green-blue
        scheme.set("Coordinator",  juce::Colour(0xffccaa66));  // amber
        scheme.set("MIDI",         juce::Colour(0xff44cc44));  // green
        scheme.set("Persistence",  juce::Colour(0xff8888cc));  // purple
        scheme.set("Sidebar",      juce::Colour(0xffaa8866));  // brown
        scheme.set("IPC",          juce::Colour(0xffcc6688));  // pink
        scheme.set("Timestamp",    juce::Colour(0xff666666));  // dim
        return scheme;
    }
};

static LogTokeniser logTokeniser;

LogPane::LogPane()
    : document(), codeEditor(document, &logTokeniser) {
    codeEditor.setReadOnly(true);
    codeEditor.setScrollbarThickness(8);
    codeEditor.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
    codeEditor.setColour(juce::CodeEditorComponent::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    codeEditor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colours::transparentBlack);
    codeEditor.setColour(juce::CodeEditorComponent::lineNumberTextId, juce::Colours::transparentBlack);
    codeEditor.setLineNumbersShown(false);
    addAndMakeVisible(codeEditor);

    exportButton.onClick = [this] { exportLogs(); };
    addAndMakeVisible(exportButton);
}

LogPane::~LogPane() {
    deactivate();
}

void LogPane::activate() {
    if (active) return;
    active = true;
    document.replaceAllContent({});
    lineCount = 0;

    if (logFile) fclose(logFile);
    logFile = fopen("/tmp/performance.log", "r");
    if (logFile) {
        juce::String content;
        char buf[4096];
        while (fgets(buf, sizeof(buf), logFile)) {
            content += juce::String::fromUTF8(buf);
            lineCount++;
        }
        document.replaceAllContent(content);
        codeEditor.moveCaretToEnd(false);
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
        bool atEnd = (codeEditor.getCaretPos().getPosition() >= document.getNumCharacters() - 1);

        document.insertText(juce::CodeDocument::Position(document, document.getNumCharacters()), newText);

        if (atEnd)
            codeEditor.moveCaretToEnd(false);
    }
}

void LogPane::resized() {
    auto area = getLocalBounds();
    auto bar = area.removeFromTop(28).reduced(6, 4);
    exportButton.setBounds(bar.removeFromRight(110));
    codeEditor.setBounds(area);
}

void LogPane::exportLogs() {
    auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%dT%H-%M-%S");
    auto suggested = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                         .getChildFile("performance-logs-" + stamp + ".txt");

    fileChooser = std::make_unique<juce::FileChooser>(
        "Export Logs", suggested, "*.txt");

    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
        [](const juce::FileChooser& fc) {
            auto out = fc.getResult();
            if (out == juce::File{}) return;

            juce::String content;

            // Rescued .prev logs first (oldest first by epoch in filename)
            auto tmpDir = juce::File("/tmp");
            auto prevs = tmpDir.findChildFiles(juce::File::findFiles, false,
                                                "performance.log.*.prev");
            std::sort(prevs.begin(), prevs.end(),
                [](const juce::File& a, const juce::File& b) {
                    return a.getFileName() < b.getFileName();
                });
            for (auto& f : prevs) {
                content += "===== " + f.getFileName() + " =====\n";
                content += f.loadFileAsString();
                content += "\n\n";
            }

            // Current log last
            auto current = juce::File("/tmp/performance.log");
            if (current.existsAsFile()) {
                content += "===== performance.log (current session) =====\n";
                content += current.loadFileAsString();
            }

            out.replaceWithText(content);
        });
}
