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
    void mouseExit(const juce::MouseEvent& event) override;
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
    static constexpr int trackHeaderWidth = 140;
    int trackRowHeight = 72;
    static constexpr int rulerHeight = 28;
    int beatsPerBar() const;

    double pixelsPerBeat = 30.0;
    bool snapToGrid = true;  // TODO: toolbar toggle
    double scrollBeat = 0.0;  // horizontal scroll position in beats
    int trackScrollY = 0;      // vertical scroll offset, pixels; 0 = top
    int totalTrackContentHeight() const;
    void clampTrackScrollY();   // 0 ≤ trackScrollY ≤ max(0, contentH - visibleH)

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

    // Visual model — derive once from raw state, then paint from this.
    // Centralises composition rules so a new visual axis (e.g. recording)
    // is one struct field + one branch in a paint helper.
    enum class Audibility { Active, Muted };

    struct TrackRowVisuals {
        Audibility       audibility;
        bool             selected;
        bool             focused;     // singular "track I'm playing into"
        TrackSourceType  type;
    };

    struct RegionVisuals {
        Audibility audibility;
        bool       selected;
        bool       beingDragged;
        bool       beingTrimmed;
    };

    TrackRowVisuals trackRowVisuals(const TrackState& t) const;
    RegionVisuals   regionVisuals(const TrackState& t, const RegionState& r) const;

    // Track-row geometry. Action tracks render at half height to keep the
    // timeline compact — event spheres don't need the vertical real estate.
    int rowHeightFor(const TrackState& t) const;
    int rowYFor(size_t trackIndex) const;  // y offset from top of track area

    // Region fill colour given its audibility — single source of truth for
    // muted-region tinting, shared by paintRegionShell and ghost-loop drawing.
    juce::Colour    regionFillColour(Audibility a) const;

    // Paint the row background (mute-aware) plus selection overlay. Used by
    // both paintTrackHeaders (header column slice) and paintGrid (lane slice).
    // Type stripe and pills are header-only and stay in paintTrackHeaders.
    void paintTrackRow(juce::Graphics& g, juce::Rectangle<int> bounds,
                       const TrackRowVisuals& v);

    // Paint a region's background, border, and selection. Content (notes,
    // waveform, name) is drawn by the caller after this so the region's
    // alpha-from-audibility flows through.
    void paintRegionShell(juce::Graphics& g, juce::Rectangle<int> bounds,
                          const RegionVisuals& v);

    // Click areas — transport buttons
    juce::Rectangle<int> rewindButtonBounds;
    juce::Rectangle<int> stopButtonBounds;
    juce::Rectangle<int> playButtonBounds;
    juce::Rectangle<int> recordButtonBounds;
    juce::Rectangle<int> cycleButtonBounds;
    juce::Rectangle<int> showActionTrackButtonBounds;  // view group, sits between transport + LCD

    // Transport button hover tracking
    enum class TransportGlyph { Rewind, Stop, Play, Record, Cycle, EventsToggle };
    enum class HoveredTransport { None, Rewind, Stop, Play, Record, Cycle, EventsToggle };
    HoveredTransport hoveredTransport = HoveredTransport::None;

    // Draws container (rest / hover / active) + glyph. activeCol is the fill
    // when active=true. When active is false the button uses bgControl +
    // bgControlHover. Glyph uses textSecondary / textPrimary / textOnColor.
    void paintTransportButton(juce::Graphics& g, juce::Rectangle<int> bounds,
                              TransportGlyph glyph, bool active, bool hovered,
                              juce::Colour activeCol);

    // BPM and time sig click areas (in transport LCD)
    juce::Rectangle<int> bpmClickBounds;
    juce::Rectangle<int> timeSigClickBounds;

    // Metronome volume slider
    juce::Slider metronomeSlider;
    juce::Label metronomeLabel;

    // Per-track icon bounds (rebuilt each paint)
    std::vector<juce::Rectangle<int>> armBounds;
    std::vector<juce::Rectangle<int>> inputMonitorBounds;
    std::vector<juce::Rectangle<int>> muteBounds;
    std::vector<juce::Rectangle<int>> soloBounds;
    // Pill hover tracking (only applied to resting/off pills)
    enum class HoveredPill { None, Mute, Solo, Arm, Input };
    int hoveredPillTrackIdx = -1;
    HoveredPill hoveredPill = HoveredPill::None;

    // Track drag reordering
    int dragTrackIndex = -1;
    int dragTargetIndex = -1;
    int dragStartY = 0;
    int getTrackIndexAtY(int y) const;

    // Inline name editing
    InlineEditor nameEditor;

    // Track selection (anchor for shift-range)
    TrackId selectionAnchorTrackId;
    void handleTrackHeaderClick(int trackIdx, const juce::MouseEvent& event);

    // Region interaction
    std::set<RegionId> selectedRegionIds;
    struct RegionHitInfo {
        RegionId regionId;
        TrackId trackId;
        juce::Rectangle<int> bounds;
    };
    std::vector<RegionHitInfo> regionHitRects;  // rebuilt each paint

    struct ActionHitInfo {
        ActionEventId eventId;
        RegionId regionId;
        TrackId trackId;
        juce::Rectangle<int> bounds;
    };
    std::vector<ActionHitInfo> actionHitRects;  // rebuilt each paint

    // Region drag state
    bool draggingRegion = false;
    bool dragIsOption = false;  // option+drag = duplicate
    RegionId dragRegionId;      // the region being dragged (for multi-select)
    double dragStartBeat = 0.0;
    int dragStartTrackIdx = -1;
    double dragCurrentBeat = 0.0;
    int dragCurrentTrackIdx = -1;

    // Loop ghost right edges (for resize cursor + drag)
    struct GhostEdgeInfo {
        RegionId regionId;
        int rightX;
        int y, height;
    };
    std::vector<GhostEdgeInfo> ghostEdgeRects;  // rebuilt each paint
    bool draggingLoopEnd = false;
    RegionId loopEndRegionId;

    // Region trim state
    enum class TrimEdge { None, Left, Right };
    TrimEdge trimEdge = TrimEdge::None;
    RegionId trimRegionId;
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
    ActionEventId dragActionEventId;
    TrackId dragActionTrackId;
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
