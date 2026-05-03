#include "gui/MidiMonitorPane.h"

namespace {

// Per-line tokeniser — same idea as LogPane's. We classify the whole
// line by scanning for one of a small set of fixed substrings (the
// type token we emit when formatting the line). One token per line;
// the CodeEditor renders it in the matching color from the scheme.
class MidiMessageTokeniser : public juce::CodeTokeniser {
public:
    enum Token { Default, NoteOn, NoteOff, CC, PitchBend, Pressure, Program, System };

    int readNextToken(juce::CodeDocument::Iterator& source) override {
        juce::String line;
        while (! source.isEOF() && source.peekNextChar() != '\n')
            line += source.nextChar();
        if (! source.isEOF()) source.nextChar();  // consume newline

        // Match in priority order — "note off" must come before "note"
        // so the substring search doesn't false-positive Note On for
        // Note Off lines.
        if (line.contains(" noteOff "))     return NoteOff;
        if (line.contains(" noteOn "))      return NoteOn;
        if (line.contains(" cc "))          return CC;
        if (line.contains(" pitchBend "))   return PitchBend;
        if (line.contains(" pressure "))    return Pressure;
        if (line.contains(" program "))     return Program;
        if (line.contains(" sysex ")
            || line.contains(" clock ")
            || line.contains(" start ")
            || line.contains(" stop ")
            || line.contains(" continue ")) return System;
        return Default;
    }

    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override {
        juce::CodeEditorComponent::ColourScheme scheme;
        scheme.set("Default",   juce::Colour(0xff999999));
        scheme.set("NoteOn",    juce::Colour(0xff44cc44));  // green
        scheme.set("NoteOff",   juce::Colour(0xff7aaa8a));  // dim green
        scheme.set("CC",        juce::Colour(0xff6a9fcc));  // blue
        scheme.set("PitchBend", juce::Colour(0xffccaa66));  // amber
        scheme.set("Pressure",  juce::Colour(0xff8888cc));  // purple
        scheme.set("Program",   juce::Colour(0xffcc6688));  // pink
        scheme.set("System",    juce::Colour(0xff666666));  // dim grey
        return scheme;
    }
};

MidiMessageTokeniser midiTokeniser;

}  // namespace

MidiMonitorPane::MidiMonitorPane()
    : document(), codeEditor(document, &midiTokeniser) {
    codeEditor.setReadOnly(true);
    codeEditor.setScrollbarThickness(8);
    codeEditor.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
    codeEditor.setColour(juce::CodeEditorComponent::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    codeEditor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colours::transparentBlack);
    codeEditor.setColour(juce::CodeEditorComponent::lineNumberTextId, juce::Colours::transparentBlack);
    codeEditor.setLineNumbersShown(false);
    addAndMakeVisible(codeEditor);

    clearButton.onClick = [this] {
        document.replaceAllContent({});
        lineCount = 0;
    };
    addAndMakeVisible(clearButton);

    setOpaque(true);
}

MidiMonitorPane::~MidiMonitorPane() = default;

void MidiMonitorPane::handleMidiActivity(const std::string& deviceName,
                                            const std::string& /*description*/,
                                            const std::string& type,
                                            int channel, int number, int value) {
    auto now = juce::Time::getCurrentTime();
    juce::String stamp = now.formatted("%H:%M:%S");
    int millis = (int)(juce::Time::getMillisecondCounterHiRes()) % 1000;
    stamp << "." << juce::String(millis).paddedLeft('0', 3);

    juce::String line;
    line << stamp << "  "
         << (deviceName.empty() ? juce::String("(unknown)") : juce::String(deviceName))
         << "  ch " << juce::String(channel).paddedLeft(' ', 2)
         << "  " << juce::String(type)        // note tokeniser keys on this exact word
         << "  #" << number
         << "  v=" << value
         << "\n";

    document.insertText(juce::CodeDocument::Position(document, document.getNumCharacters()),
                         line);
    ++lineCount;
    if (lineCount > maxLines) trimToCap();

    // Auto-scroll to tail unless the user has scrolled away (caret not
    // at the end means they're reading earlier content; respect that).
    if (codeEditor.getCaretPos().getPosition() >= document.getNumCharacters() - line.length() - 2)
        codeEditor.moveCaretToEnd(false);
}

void MidiMonitorPane::trimToCap() {
    // Drop the oldest 20% so we trim in chunks rather than per-line.
    int trimLines = lineCount - (maxLines * 4 / 5);
    if (trimLines <= 0) return;

    auto fullText = document.getAllContent();
    int charsToDrop = 0;
    int linesDropped = 0;
    for (int i = 0; i < fullText.length() && linesDropped < trimLines; ++i) {
        if (fullText[i] == '\n') {
            ++linesDropped;
            charsToDrop = i + 1;
        }
    }
    if (charsToDrop > 0) {
        document.replaceAllContent(fullText.substring(charsToDrop));
        lineCount -= linesDropped;
    }
}

void MidiMonitorPane::resized() {
    auto area = getLocalBounds();
    auto bar = area.removeFromTop(28).reduced(6, 4);
    clearButton.setBounds(bar.removeFromRight(80));
    codeEditor.setBounds(area);
}
