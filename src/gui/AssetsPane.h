#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "state/StateModel.h"
#include <vector>

class StateAPI;

// Assets pane — a tall, skinny browser (Right slot) listing the current
// song's distinct persisted audio files, grouped by origin category
// (Recorded / Imported / Other). Rows are drag sources: drag one onto the
// Looper lane to place it as a loop. Dedup is by file path (the same file
// can be referenced by many regions/takes; it appears once).
//
// Drag payload is a DynamicObject { kind:"asset", filePath, origin, name },
// mirroring ControllersPane's drag idiom; the DragAndDropContainer is the
// shared MainLayout. See docs — phase 1 (no first-class asset registry;
// this is a view over existing take files).
class AssetsPane : public juce::Component,
                   private juce::Timer {
public:
    explicit AssetsPane(StateAPI& state);
    ~AssetsPane() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void visibilityChanged() override;

private:
    StateAPI& state;
    int stateSubId = -1;

    struct Asset {
        juce::String filePath;   // dedup key + drag payload
        juce::String name;       // display label (owner track name)
        juce::String origin;     // "recorded" | "imported" | "unknown"
        double lengthBeats = 0.0;
        double recordTempo = 120.0;
        int sampleRate = 48000;
        int channelCount = 2;
        std::vector<std::pair<float, float>> thumb;  // downsampled {min,max}
    };
    struct Row {
        bool isHeader = false;
        juce::String headerLabel;
        int assetIndex = -1;              // into `assets` when !isHeader
        juce::Rectangle<int> bounds;      // content-space (pre-scroll)
    };
    std::vector<Asset> assets;
    std::vector<Row> rows;
    int contentHeight = 0;
    int scrollY = 0;

    // Drag arming (mirrors ControllersPane).
    int pendingDragRow = -1;
    juce::Point<int> pendingDragStart;
    bool dragInProgress = false;
    int hoverRow = -1;

    // True when an enumerated file has no peaks yet (still being computed on
    // load). While set, a low-rate timer retries rebuild() until peaks land.
    bool peaksPending = false;
    void timerCallback() override;

    void rebuild();       // re-enumerate assets from state
    void layoutRows();    // recompute row bounds + contentHeight
    void clampScroll();
    int rowAtPoint(juce::Point<int> p) const;  // -1 if none; content-space aware

    void paintAssetRow(juce::Graphics& g, const Asset& a, juce::Rectangle<int> b, bool hover);

    static std::vector<std::pair<float, float>> downsamplePeaks(
        const std::vector<std::pair<float, float>>& src, int bins);

    static constexpr int headerRowH = 22;
    static constexpr int assetRowH  = 48;
    static constexpr int rowGap     = 1;
    static constexpr int thumbBins  = 120;
};
