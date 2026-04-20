#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "state/StateModel.h"
#include <string>
#include <vector>

class StateAPI;
class EngineAPI;
class PerformanceCoordinator;

// Song Mappings pane — right side of the Performer view.
// Two subsections: Atemporal (always-active bindings) and Score (ordered
// one-time actions). Drag source for "mapping" / "score" payloads. Drop
// target for "control", "mapping", "score" kinds.

class SongMappingsPane : public juce::Component,
                         public juce::DragAndDropTarget,
                         private juce::Timer {
public:
    SongMappingsPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator);
    ~SongMappingsPane() override;

    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    // MIDI activity — called by MainLayout's shared global monitor, which
    // dispatches to both this pane and ControllersPane so activity dots
    // pulse in both places.
    void handleMidiActivity(const std::string& type, int channel, int number,
                            const std::string& deviceId);

private:
    StateAPI& state;
    EngineAPI& engine;
    PerformanceCoordinator& coordinator;
    int stateSubscriptionId = -1;

    struct MappingRow {
        DeviceId deviceId;
        std::string deviceName;
        std::string controlName;
        std::string group;
        std::string controlType;
        int channel = 0;
        int number = 0;
        int controlIndex = -1;
        BindingId bindingId;
        std::string actionLabel;
        std::string argsDisplay;
        int64_t lastActivityMs = 0;
    };

    struct ScoreRow {
        DeviceId deviceId;
        std::string deviceName;
        std::string controlName;
        std::string controlType;
        int channel = 0;
        int number = 0;
        BindingId bindingId;
        std::string actionLabel;
        std::string argsDisplay;
        int scorePosition = 0;
        int64_t lastActivityMs = 0;
    };

    std::vector<MappingRow> mappingRows;
    std::vector<ScoreRow> scoreRows;

    juce::Rectangle<int> mappingPanelBounds, scorePanelBounds;
    int mappingScrollOffset = 0;

    int pendingDragMappingRow = -1;
    int pendingDragScoreRow = -1;
    juce::Point<int> pendingDragStart;
    bool dragInProgress = false;

    enum class DropTarget { None, Mappings, Score };
    DropTarget currentDropTarget = DropTarget::None;
    juce::Point<int> dropCursor;

    int hoveredMappingRow = -1, hoveredScoreRow = -1;

    void buildRows();
    void resetPendingDrag();

    void paintAtemporalSection(juce::Graphics& g);
    void paintScoreSection(juce::Graphics& g);

    void showActionMenu(const DeviceId& deviceId, const std::string& ctrlType,
                        int channel, int number, const std::string& controlName,
                        const BindingId& existingBindingId,
                        juce::Point<int> screenPos, bool asScoreStep);
    void showControlPicker(bool forScore, juce::Point<int> screenPos);

    std::string formatArgs(const ActionInfo* action, const std::string& argsJson) const;
    bool isDeviceConnected(const std::string& deviceId) const;

    static constexpr int sectionTitleHeight = 38;
    static constexpr int rightHeaderHeight = 42;
    static constexpr int rowHeight = 28;
    static constexpr int scoreMinHeight = 130;
    static constexpr int panelPadding = 8;

    void timerCallback() override;
};
