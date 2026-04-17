#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/ControllersPane.h"
#include "gui/SongMappingsPane.h"
#include "gui/Divider.h"

class StateAPI;
class EngineAPI;
class PerformanceCoordinator;

// Performer view — a single pane composing the Controllers source (left
// half) and the SongMappings target/editor (right half), separated by a
// draggable internal divider. Replaces the prior arrangement where the
// two were independent panes sharing the outer Left/Right slots; they
// now act as one pane for sidebar toggling and slot assignment.
class PerformPane : public juce::Component {
public:
    PerformPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coord);

    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    ControllersPane& controllers() { return controllersPane; }
    SongMappingsPane& songMappings() { return songMappingsPane; }

private:
    ControllersPane controllersPane;
    SongMappingsPane songMappingsPane;
    Divider divider { Divider::Vertical };

    float controllersRatio = 0.32f;  // portion of width given to Controllers
    int dragStartControllersWidth = 0;

    static constexpr int minSide = 160;
};
