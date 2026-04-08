#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/InlineEditor.h"
#include <functional>
#include <string>
#include <vector>

class StateAPI;
class PerformanceCoordinator;

class DeviceEditorPane : public juce::Component, private juce::Timer {
public:
    DeviceEditorPane(StateAPI& state, PerformanceCoordinator& coordinator);
    ~DeviceEditorPane() override;

    void setDevice(const std::string& deviceId, const std::string& portName);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    StateAPI& state;
    PerformanceCoordinator& coordinator;
    std::string currentDeviceId;

    // Header
    juce::Label deviceNameLabel;
    juce::Label portNameLabel;

    // Control list — simple painted rows
    struct ControlRow {
        std::string name;
        std::string controlType;
        int channel = 0;
        int number = 0;
        std::string group;
        int64_t lastActivityMs = 0;
    };
    std::vector<ControlRow> controls;

    // Learn mode
    juce::TextButton learnButton;
    juce::Label statusLabel;
    bool isLearning = false;

    // MIDI event log (last 2 events shown in header)
    juce::Label midiEventLabel;
    std::string lastEvent1;
    std::string lastEvent2;
    void onMidiEvent(const std::string& description,
                     const std::string& type, int channel, int number);

    // Inline editing
    InlineEditor inlineEditor;

    // Pending learn control (for Escape cancellation)
    int pendingLearnControlIndex = -1;

    // Hover state for delete buttons
    int hoveredRow = -1;

    void refreshControls();
    void startLearn();
    void cancelLearn();
    void onLearnCapture(const std::string& type, int channel, int number);

    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

    static constexpr int headerHeight = 60;
    static constexpr int rowHeight = 24;
    static constexpr int bottomBarHeight = 40;
    static constexpr int columnHeaderHeight = 24;

    int stateSubscriptionId = -1;

    // Timer for activity light decay
    void timerCallback() override;

    // Column layout helpers
    juce::Rectangle<int> getRowBounds(int rowIndex) const;
    juce::Rectangle<int> getNameCellBounds(int rowIndex) const;
    juce::Rectangle<int> getDeleteButtonBounds(int rowIndex) const;
    juce::Rectangle<int> getControlListArea() const;
};
