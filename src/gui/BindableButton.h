#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "api/StateAPI.h"
#include "gui/Theme.h"

class PerformanceCoordinator;

// A segmented-strip button that wraps a coordinator action. Three
// stacked layers per cell:
//   1. Label / icon    — the action's affordance.
//   2. Activity light  — flashes when the action fires (from any
//                        path: GUI click, MIDI binding, Lua call).
//                        Wired via PerformanceCoordinator's
//                        addActionFireListener so it stays in sync
//                        without polling.
//   3. Binding readout — name of the currently-mapped control, or a
//                        "+ set" affordance when nothing's bound.
//                        Click → set-control popup (MIDI Learn).
//
// CornerStyle controls outer-corner rounding so multiple buttons
// can render as a single segmented strip with rounded outer ends and
// flat inner edges.
//
// The activePredicate lights the cell in accent color while a stateful
// condition holds (e.g. ReplaceQueued/CapturingReplace for the
// replace button). Decoupled from the activity flash.
class BindableButton : public juce::Component {
public:
    enum class Variant { TextLabel, IconPlay, IconArrowUp, IconArrowDown };
    enum CornerStyle { Solo, Left, Mid, Right };

    BindableButton(StateAPI& state,
                    PerformanceCoordinator& coord,
                    juce::String actionName,
                    juce::String label,
                    Variant variant = Variant::TextLabel);
    ~BindableButton() override;

    void setCornerStyle(CornerStyle s) { corners = s; repaint(); }
    void setActivePredicate(std::function<bool()> p) {
        activePred = std::move(p); repaint();
    }
    void setEnabledPredicate(std::function<bool()> p) {
        enabledPred = std::move(p); repaint();
    }
    // Optional 3px color bar along the top edge — used as a category
    // hint (e.g. Replace and Overdub get color-keyed stripes that
    // match their respective lane-state tints).
    void setTopColorStripe(juce::Colour c) { topStripe = c; repaint(); }
    // Show a small filled red circle to the left of the label, in
    // the visual family of the Producer's record button. Used by
    // Replace/Overdub so they read as "record (replace)" / "record
    // (overdub)" rather than as standalone words.
    void setShowRecordDot(bool b) { showRecordDot = b; repaint(); }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    static constexpr int desiredHeight = 64;

private:
    StateAPI& state;
    PerformanceCoordinator& coord;
    juce::String actionName;
    juce::String label;
    Variant variant;
    CornerStyle corners = Solo;
    std::function<bool()> activePred;
    std::function<bool()> enabledPred;

    int actionFireSubId = -1;
    juce::int64 litUntilMs = 0;
    juce::Colour topStripe;     // transparent = no stripe
    bool showRecordDot = false;

    juce::Rectangle<int> bindingRowBounds() const;
    juce::String currentBindingName() const;  // "" when unbound

    void showSetControlPopup();
    void paintCellBackground(juce::Graphics&, juce::Colour fill);
    void paintIcon(juce::Graphics&, juce::Rectangle<int> area, juce::Colour col);
    bool isActive() const { return activePred && activePred(); }
    bool isEnabled() const { return !enabledPred || enabledPred(); }
    bool isLit() const;
};
