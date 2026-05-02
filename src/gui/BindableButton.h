#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include "api/StateAPI.h"
#include "gui/Theme.h"

class PerformanceCoordinator;

// A segmented-strip button that wraps a coordinator action. Two
// stacked layers per cell:
//   1. Label / icon  — the action's affordance.
//   2. Status dot    — encodes binding + activity in one mark:
//                      • dim grey  = no control bound
//                      • soft green = a control is bound
//                      • accent     = this button is armed in MIDI Learn
//                      • yellow flash overlays for ~200ms when the action
//                        fires (any source: GUI, MIDI, Lua). Wired via
//                        PerformanceCoordinator::addActionFireListener so
//                        the dot stays in sync without polling.
//
// CornerStyle controls outer-corner rounding so multiple buttons render
// as a single segmented strip with rounded outer ends and flat inner
// edges.
//
// activePredicate lights the cell with a subtle inset background while
// a stateful condition holds (e.g. CapturingReplace for the replace
// button). Decoupled from the activity flash.
//
// Learn mode: when learnPredicate returns true, mouseDown calls
// onArmRequest instead of executing the action — the host pane is then
// responsible for capturing the next MIDI event and creating a binding.
// The owning component should also drive armedPredicate so the dot can
// reflect "this is the active arm target."
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

    // Learn-mode wiring (set by the host pane that runs the learn flow).
    // learnPredicate: when it returns true, clicks arm instead of fire.
    // armedPredicate: when it returns true, the status dot shows the
    //                 accent "armed" color.
    // onArmRequest:   called on click while learn mode is active.
    void setLearnPredicate(std::function<bool()> p) { learnPred = std::move(p); repaint(); }
    void setArmedPredicate(std::function<bool()> p) { armedPred = std::move(p); repaint(); }
    void setOnArmRequest(std::function<void()> fn) { onArmRequest = std::move(fn); }

    // Action name for callers that want to wire learn flow / context
    // menu lookups against this button's bound action.
    const juce::String& getActionName() const { return actionName; }

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
    std::function<bool()> learnPred;
    std::function<bool()> armedPred;
    std::function<void()> onArmRequest;

    int actionFireSubId = -1;
    juce::int64 litUntilMs = 0;
    juce::Colour topStripe;     // transparent = no stripe
    bool showRecordDot = false;

    // Returns the binding (if any) for our action in the current scope —
    // used to decide bound-vs-unbound dot color and to populate the
    // right-click menu. Returns nullopt when no matching binding exists.
    std::optional<BindingState> findBindingForAction() const;

    void showContextMenu();
    void paintCellBackground(juce::Graphics&, juce::Colour fill);
    void paintIcon(juce::Graphics&, juce::Rectangle<int> area, juce::Colour col);
    bool isActive() const { return activePred && activePred(); }
    bool isEnabled() const { return !enabledPred || enabledPred(); }
    bool isLearnMode() const { return learnPred && learnPred(); }
    bool isArmed() const { return armedPred && armedPred(); }
    bool isLit() const;
};
