#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "gui/Theme.h"
#include <string>

// Right-slot pane that shows real-time MIDI events as a scrolling
// selectable + colored text view. Mirrors LogPane's CodeEditor +
// CodeTokeniser pattern (selectable text, custom per-line coloring,
// fixed-cap tail), but pulls events from MIDIEngine's monitor
// callback instead of polling /tmp/performance.log.
//
// Wiring: MainLayout fans out the global MIDI monitor callback to
// PerformPane (existing) AND this pane via handleMidiActivity().
//
// Lines are color-coded by message type — note on / note off / cc /
// pitch bend / pressure / program change / system. Useful for
// confirming a controller is sending what you think it is, and for
// general "is anything coming through?" debugging without leaving
// the app.
class MidiMonitorPane : public juce::Component {
public:
    MidiMonitorPane();
    ~MidiMonitorPane() override;

    void resized() override;

    // Called from the MIDIEngine global monitor callback (via
    // MainLayout's fan-out). Safe to call from the message thread.
    void handleMidiActivity(const std::string& deviceName,
                              const std::string& description,
                              const std::string& type,
                              int channel, int number, int value);

private:
    juce::CodeDocument document;
    juce::CodeEditorComponent codeEditor;
    juce::TextButton clearButton { "Clear" };
    int lineCount = 0;
    static constexpr int maxLines = 5000;

    // Trim oldest lines when we exceed maxLines so the document doesn't
    // grow unbounded for a long-running session.
    void trimToCap();
};
