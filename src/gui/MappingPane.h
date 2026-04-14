#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/InlineEditor.h"
#include "state/StateModel.h"
#include <string>
#include <vector>
#include <map>
#include <set>

class StateAPI;
class EngineAPI;
class PerformanceCoordinator;

// Performance Map — three-panel view of all MIDI device controls and bindings.
// Left: available (unmapped) controls grouped by device.
// Right top: always-active mappings (non-score bindings), sorted by device/group.
// Right bottom: score steps in performance order.

class MappingPane : public juce::Component, private juce::Timer {
public:
    MappingPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator);
    ~MappingPane() override;

    void scrollToDevice(const std::string& deviceId);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

private:
    StateAPI& state;
    EngineAPI& engine;
    PerformanceCoordinator& coordinator;
    int stateSubscriptionId = -1;

    // --- Row models ---
    struct LeftPanelEntry {
        enum Type { DeviceHeader, Control };
        Type type;
        std::string deviceId;
        std::string deviceName;
        bool deviceConnected = false;
        // Control fields (when type == Control)
        std::string controlName;
        std::string group;
        std::string controlType;
        int channel = 0;
        int number = 0;
        int controlIndex = -1;
        int64_t lastActivityMs = 0;
    };

    struct MappingRow {
        std::string deviceId;
        std::string deviceName;
        std::string controlName;
        std::string group;
        std::string controlType;
        int channel = 0;
        int number = 0;
        int controlIndex = -1;
        std::string bindingId;
        std::string actionLabel;
        std::string argsDisplay;
        int64_t lastActivityMs = 0;
    };

    struct ScoreRow {
        std::string deviceId;
        std::string deviceName;
        std::string controlName;
        std::string controlType;
        int channel = 0;
        int number = 0;
        std::string bindingId;
        std::string actionLabel;
        std::string argsDisplay;
        int scorePosition = 0;
        int64_t lastActivityMs = 0;
    };

    std::vector<LeftPanelEntry> leftEntries;
    std::vector<MappingRow> mappingRows;
    std::vector<ScoreRow> scoreRows;

    // --- Panel bounds ---
    juce::Rectangle<int> leftPanelBounds, mappingPanelBounds, scorePanelBounds;
    int leftScrollOffset = 0, mappingScrollOffset = 0, scoreScrollOffset = 0;

    // --- Hover ---
    int hoveredLeftRow = -1, hoveredMappingRow = -1, hoveredScoreRow = -1;

    // --- Build ---
    void buildRows();
    void refresh();

    // --- Paint helpers ---
    void paintHeader(juce::Graphics& g);
    void paintLeftPanel(juce::Graphics& g);
    void paintMappingPanel(juce::Graphics& g);
    void paintScorePanel(juce::Graphics& g);

    // --- Interactions ---
    void showActionMenu(const std::string& deviceId, const std::string& ctrlType,
                        int channel, int number, const std::string& controlName,
                        const std::string& existingBindingId,
                        juce::Point<int> screenPos, bool asScoreStep);
    void showControlPicker(bool forScore, juce::Point<int> screenPos);

    // --- Learn mode ---
    juce::TextButton learnButton;
    bool isLearning = false;
    InlineEditor inlineEditor;
    void startLearn();
    void cancelLearn();
    void armLearnCapture();
    void onLearnCapture(const std::string& type, int channel, int number,
                        const std::string& portName);

    // --- Activity ---
    void onMidiEvent(const std::string& type, int channel, int number,
                     const std::string& deviceId);

    // --- Utility ---
    juce::Colour getDeviceColor(const std::string& deviceId) const;
    std::string formatArgs(const std::string& argsJson) const;
    bool isDeviceConnected(const std::string& deviceId) const;

    // --- Layout constants ---
    static constexpr int headerHeight = 44;
    static constexpr int sectionTitleHeight = 28;
    static constexpr int rowHeight = 24;
    static constexpr int scoreRowHeight = 28;
    static constexpr int leftPanelWidth = 220;
    static constexpr int panelPadding = 8;

    void timerCallback() override;
};
