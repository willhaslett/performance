#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/InlineEditor.h"
#include "state/StateModel.h"
#include <string>
#include <vector>
#include <set>
#include <functional>

class StateAPI;
class EngineAPI;
class PerformanceCoordinator;

// Controllers pane — left panel of the Performer view.
// Collapsible device tree showing ALL MIDI controls with Learn mode.
// Drag source for "control" payloads. Not a drop target.

class ControllersPane : public juce::Component,
                        private juce::Timer {
public:
    ControllersPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator);
    ~ControllersPane() override;

    void scrollToDevice(const std::string& deviceId);

    // Called when learn mode creates a new binding / touches state that
    // the sibling SongMappingsPane should refresh for.
    std::function<void()> onBindingsChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

    // Called by MainLayout's shared MIDI monitor to update activity dots.
    void handleMidiActivity(const std::string& type, int channel, int number,
                            const std::string& deviceId);

private:
    StateAPI& state;
    EngineAPI& engine;
    PerformanceCoordinator& coordinator;
    int stateSubscriptionId = -1;

    struct LeftPanelEntry {
        enum Type { DeviceHeader, Control };
        Type type;
        std::string deviceId;
        std::string deviceName;
        bool deviceConnected = false;
        std::string controlName;
        std::string group;
        std::string controlType;
        int channel = 0;
        int number = 0;
        int controlIndex = -1;
        bool isBound = false;
        int64_t flashMs = 0;
        int64_t lastActivityMs = 0;
    };

    std::vector<LeftPanelEntry> leftEntries;

    int leftScrollOffset = 0;
    int lastMidiDeviceCount = -1;
    std::set<std::string> collapsedDevices;

    // Pending drag promotion
    int pendingDragLeftRow = -1;
    juce::Point<int> pendingDragStart;
    bool dragInProgress = false;

    int hoveredLeftRow = -1;

    void buildRows();
    void refresh();
    void resetPendingDrag();

    bool isLearning = false;
    InlineEditor inlineEditor;
    int editingLeftRow = -1;
    void openLeftEditor(int entryIndex);
    void startLearn();
    void cancelLearn();
    void armLearnCapture();
    void onLearnCapture(const std::string& type, int channel, int number,
                        const std::string& portName);

    void onMidiEvent(const std::string& type, int channel, int number,
                     const std::string& deviceId);

    bool isDeviceConnected(const std::string& deviceId) const;

    static constexpr int leftHeaderHeight = 42;
    static constexpr int rowHeight = 28;
    static constexpr int panelPadding = 8;

    void timerCallback() override;
};
