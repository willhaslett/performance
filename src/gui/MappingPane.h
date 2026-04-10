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

// Unified device mapping + bindings + score pane.
// Shows one MIDI device's mapped controls in two sections:
// 1. Global bindings (always active, no score steps)
// 2. Song bindings (song-scoped, with score step column)
//
// Each row: [activity] [name] [group] [type] [ch] [#] [action] [score]
class MappingPane : public juce::Component, private juce::Timer {
public:
    MappingPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator);
    ~MappingPane() override;

    void setDevice(const std::string& deviceId, const std::string& portName);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    StateAPI& state;
    EngineAPI& engine;
    PerformanceCoordinator& coordinator;

    std::string currentDeviceId;
    int stateSubscriptionId = -1;

    // Header
    juce::Label deviceNameLabel;
    juce::Label portNameLabel;
    juce::TextButton learnButton;
    juce::Label midiLabelPrefix;
    juce::Label midiEventLabel;
    std::string lastEvent1, lastEvent2;

    // Learn mode
    bool isLearning = false;
    int pendingLearnControlIndex = -1;
    InlineEditor inlineEditor;

public:
    // Flattened row model (public for isSectionHeader helper)
    struct Row {
        enum Section { GlobalHeader, GlobalControl, SongHeader, SongControl, ScoreHeader, ScoreControl };
        Section section;
        // Control data (for control rows)
        std::string controlName;
        std::string group;
        std::string controlType;
        int channel = 0;
        int number = 0;
        int controlIndex = -1;  // index in device.controls
        // Binding data
        std::string bindingId;
        std::string actionName;
        std::string argsDisplay;
        bool isGlobal = false;
        // Score step
        int scorePosition = -1;  // -1 = not a score step
        // Activity
        int64_t lastActivityMs = 0;
    };
private:
    std::vector<Row> rows;
    int hoveredRow = -1;
    int scrollOffset = 0;

    void buildRows();
    void refresh();
    void startLearn();
    void cancelLearn();
    void armLearnCapture();
    void onLearnCapture(const std::string& type, int channel, int number);
    void onMidiEvent(const std::string& description,
                     const std::string& type, int channel, int number);

    void showActionMenu(int rowIndex, juce::Point<int> screenPos);
    void showArgsPopup(const Row& row, const ActionInfo& action);
    void showScoreMenu(int rowIndex, juce::Point<int> screenPos);
    void showGroupMenu(int rowIndex, juce::Point<int> screenPos);

    std::string formatArgs(const std::string& argsJson) const;
    std::set<std::string> getExistingGroups() const;

    // Binding lookup
    struct ControlKey {
        std::string type; int channel; int number; std::string deviceId;
        bool operator<(const ControlKey& o) const {
            if (type != o.type) return type < o.type;
            if (channel != o.channel) return channel < o.channel;
            if (number != o.number) return number < o.number;
            return deviceId < o.deviceId;
        }
    };
    std::map<ControlKey, BindingState> songBindingMap;

    // Layout
    static constexpr int headerHeight = 60;
    static constexpr int sectionHeaderHeight = 24;
    static constexpr int scoreSectionGap = 20;
    static constexpr int rowHeight = 24;

    juce::Rectangle<int> getRowBounds(int rowIndex) const;
    int getRowY(int rowIndex) const;
    int getTotalHeight() const;

    // Column positions
    static constexpr int colActivity = 8;
    static constexpr int colScore = 24;     // Score Step toggle
    static constexpr int colName = 74;      // MIDI Source
    static constexpr int colGroup = 180;
    static constexpr int colType = 270;
    static constexpr int colCh = 318;
    static constexpr int colNum = 346;
    static constexpr int colAction = 380;
    static constexpr int titleHeight = 24;  // "Mappings" title above column headers

    void timerCallback() override;
};
