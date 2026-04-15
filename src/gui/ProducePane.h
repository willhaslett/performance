#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include "gui/Theme.h"
#include "gui/InlineEditor.h"
#include "daw/SequencerAPI.h"
#include "daw/Arrangement.h"

class StateAPI;

// DAW-style arrange view: transport bar, track headers, timeline grid with regions.

class ProducePane : public juce::Component, public juce::DragAndDropContainer, private juce::Timer {
public:
    ProducePane();
    ~ProducePane() override;

    void setState(StateAPI* state, SequencerAPI* sequencer, Arrangement* arrangement);

    // Record mode callbacks (set by coordinator via MainLayout)
    std::function<void()> onStartRecordMode;
    std::function<void()> onStopRecordMode;
    std::function<bool()> onIsRecordMode;
    std::function<void()> onRegionsChanged;  // reload audio files after move/duplicate/delete

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    StateAPI* state = nullptr;
    SequencerAPI* sequencer = nullptr;
    Arrangement* arrangement = nullptr;
    int stateSubscriptionId = -1;

    void timerCallback() override;

    // Layout
    static constexpr int transportHeight = 60;
    static constexpr int trackHeaderWidth = 180;
    int trackRowHeight = 48;
    static constexpr int rulerHeight = 28;
    int beatsPerBar() const;

    double pixelsPerBeat = 30.0;
    bool snapToGrid = true;  // TODO: toolbar toggle
    double scrollBeat = 0.0;  // horizontal scroll position in beats
    int scrollTrack = 0;       // vertical scroll (future)

    // Convert beat to x pixel
    int beatToX(double beat) const;
    double xToBeat(int x) const;
    double snapBeatToGrid(double beat) const;  // snap to nearest division
    void saveZoomState();
    void ensurePlayheadVisible();              // scroll so playhead is on-screen

    // Paint helpers
    void paintTransport(juce::Graphics& g, juce::Rectangle<int> area);
    void paintRuler(juce::Graphics& g, juce::Rectangle<int> area);
    void paintTrackHeaders(juce::Graphics& g, juce::Rectangle<int> area);
    void paintGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area);

    // Click areas — transport buttons
    juce::Rectangle<int> rewindButtonBounds;
    juce::Rectangle<int> stopButtonBounds;
    juce::Rectangle<int> playButtonBounds;
    juce::Rectangle<int> recordButtonBounds;
    juce::Rectangle<int> cycleButtonBounds;

    // BPM and time sig click areas (in transport LCD)
    juce::Rectangle<int> bpmClickBounds;
    juce::Rectangle<int> timeSigClickBounds;

    // Metronome volume slider
    juce::Slider metronomeSlider;
    juce::Label metronomeLabel;

    // Per-track icon bounds (rebuilt each paint)
    std::vector<juce::Rectangle<int>> powerIconBounds;
    std::vector<juce::Rectangle<int>> armBounds;
    std::vector<juce::Rectangle<int>> inputMonitorBounds;
    std::vector<juce::Rectangle<int>> muteBounds;
    std::vector<juce::Rectangle<int>> soloBounds;

    // Track drag reordering
    int dragTrackIndex = -1;
    int dragTargetIndex = -1;
    int dragStartY = 0;
    int getTrackIndexAtY(int y) const;

    // Inline name editing
    InlineEditor nameEditor;
    void paintPowerIcon(juce::Graphics& g, juce::Rectangle<int> iconArea, bool enabled);

    // Track selection (anchor for shift-range)
    std::string selectionAnchorTrackId;
    void handleTrackHeaderClick(int trackIdx, const juce::MouseEvent& event);

    // Region interaction
    std::set<std::string> selectedRegionIds;
    struct RegionHitInfo {
        std::string regionId;
        std::string trackId;
        juce::Rectangle<int> bounds;
    };
    std::vector<RegionHitInfo> regionHitRects;  // rebuilt each paint

    struct ActionHitInfo {
        std::string eventId;
        std::string regionId;
        std::string trackId;
        juce::Rectangle<int> bounds;
    };
    std::vector<ActionHitInfo> actionHitRects;  // rebuilt each paint

    // Region drag state
    bool draggingRegion = false;
    bool dragIsOption = false;  // option+drag = duplicate
    std::string dragRegionId;   // the region being dragged (for multi-select)
    double dragStartBeat = 0.0;
    int dragStartTrackIdx = -1;
    double dragCurrentBeat = 0.0;
    int dragCurrentTrackIdx = -1;

    // Loop ghost right edges (for resize cursor + drag)
    struct GhostEdgeInfo {
        std::string regionId;
        int rightX;
        int y, height;
    };
    std::vector<GhostEdgeInfo> ghostEdgeRects;  // rebuilt each paint
    bool draggingLoopEnd = false;
    std::string loopEndRegionId;

    // Region trim state
    enum class TrimEdge { None, Left, Right };
    TrimEdge trimEdge = TrimEdge::None;
    std::string trimRegionId;
    double trimOrigStartBeat = 0.0;
    double trimOrigLengthBeats = 0.0;
    static constexpr int trimHandleWidth = 6;

    // Cycle drag (ruler)
    bool draggingCycle = false;
    double cycleAnchorBeat = 0.0;
    enum class CycleEdge { None, Start, End };
    CycleEdge draggingCycleEdge = CycleEdge::None;
    bool draggingCycleBody = false;
    double cycleBodyDragOffset = 0.0;  // beat offset from drag start to cycle start
    static constexpr int cycleEdgeThreshold = 6;  // pixels

    // Action event drag
    std::string dragActionEventId;
    std::string dragActionTrackId;
    bool draggingActionEvent = false;

    // Quantize
    static double quantizeGridSize(int menuId);  // menu ID → beat grid size

    // Action event creation
    void showActionPicker(juce::Point<int> screenPos, const std::string& trackId, double beat);
    void showMorphEditor(const std::string& trackId, double beat,
                         const std::string& existingEventId = "");

    // Join
    void joinSelectedRegions();
};
