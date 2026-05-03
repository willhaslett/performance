#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include "api/StateAPI.h"
#include "gui/Theme.h"

class PerformanceCoordinator;

// A segmented-strip button that wraps a coordinator action. Two
// stacked layers per cell:
//   1. Label / icon  — the action's affordance.
//   2. Trigger slot  — a plugin-slot-style affordance showing the
//                      bound MIDI control (or "Trigger" placeholder
//                      if unbound). Click opens a menu of registered
//                      controls; "Manage controls" jumps to the
//                      Perform pane.
//
// On action fire (any source — GUI, MIDI, Lua) the whole cell flashes
// in `triggerLight` for ~200ms. Wired via PerformanceCoordinator's
// addActionFireListener so the flash stays in sync without polling.
//
// CornerStyle controls outer-corner rounding so multiple buttons render
// as a single segmented strip with rounded outer ends and flat inner
// edges.
//
// activePredicate lights the cell with a subtle inset background while
// a stateful condition holds (e.g. CapturingReplace for the replace
// button). Decoupled from the activity flash.
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
    // Called when the user picks "Manage controls" from the trigger
    // menu — host pane is responsible for hiding itself and revealing
    // the Perform pane.
    void setOnManageControlsRequest(std::function<void()> fn) {
        onManageControlsRequest = std::move(fn);
    }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

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
    std::function<void()> onManageControlsRequest;

    int actionFireSubId = -1;
    juce::int64 litUntilMs = 0;
    bool slotHovered = false;

    juce::Rectangle<int> triggerSlotBounds() const;

    // Returns the binding (if any) for our action in the current scope —
    // used to decide slot text and to populate the trigger menu's
    // "currently bound" tick. Returns nullopt when no matching binding
    // exists.
    std::optional<BindingState> findBindingForAction() const;
    juce::String slotDisplayText(const std::optional<BindingState>& b) const;

    void showTriggerMenu(juce::Point<int> screenPos);
    void paintCellBackground(juce::Graphics&, juce::Colour fill);
    void paintIcon(juce::Graphics&, juce::Rectangle<int> area, juce::Colour col);
    bool isActive() const { return activePred && activePred(); }
    bool isEnabled() const { return !enabledPred || enabledPred(); }
    bool isLit() const;
};
