#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/ActionInstanceForm.h"
#include "state/StateModel.h"
#include <functional>
#include <memory>
#include <vector>

class StateAPI;

// ActionPicker — two-state container for creating an action instance:
//
//   State 1: a vertical menu of available actions, env-filtered (actions whose
//            required refs have no candidates are disabled).
//   State 2: an embedded ActionInstanceForm for the picked action.
//
// Escape semantics:
//   State 2 → State 1   (back to action list)
//   State 1 → dismiss   (close the popup)
//
// Same window throughout; no submenu-flyouts. Callers anchor the popup near
// the click site via the screenPos arg to launch().
class ActionPicker : public juce::Component {
public:
    using Filter = std::function<bool(const ActionInfo&)>;

    ActionPicker(StateAPI& state, Filter filter = {});

    // Pre-fill the first arg of the action's form (e.g. when SongMappingsPane
    // is editing an existing binding and pre-filling its known args).
    void setInitialArgs(const juce::var& args);

    // Skip the picker entirely and jump to the form for this action — used by
    // the "edit existing instance" flow where the action is already known.
    void setInitialAction(const ActionInfo& action);

    std::function<void(const ActionInfo&, const juce::var& args)> onAccept;
    std::function<void()> onDismiss;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    int  getDesiredHeight() const;
    static constexpr int panelWidth = 420;

    // Launch in a borderless themed window anchored at screenPos. The window
    // takes keyboard focus and dismisses on escape (handled inside the picker).
    static void launch(StateAPI& state, juce::Point<int> screenPos,
                       Filter filter,
                       std::function<void(const ActionInfo&, const juce::var&)> onAccept);

    // Edit-existing variant: jumps straight to the form for `action`.
    static void launchEdit(StateAPI& state, juce::Point<int> screenPos,
                           const ActionInfo& action, const juce::var& initialArgs,
                           std::function<void(const ActionInfo&, const juce::var&)> onAccept);

private:
    enum class Mode { PickAction, FillParams };
    Mode mode = Mode::PickAction;

    StateAPI& state;
    Filter    filter;
    juce::var pendingInitialArgs;

    // State 1 — hand-painted list of action rows with hover highlight, like
    // our other context menus. No buttons.
    std::vector<ActionInfo> filteredActions;
    std::vector<bool>       actionEnabled;   // parallel to filteredActions
    int                     hoveredRow = -1;

    // State 2
    std::unique_ptr<ActionInstanceForm> form;

    void rebuildFilteredActions();
    void enterFillMode(const ActionInfo& action);
    void backToPickMode();
    void resizeWindowToContent();
    int  rowAt(juce::Point<int> p) const;  // -1 if none

    static constexpr int actionRowHeight = 28;
    static constexpr int menuTopPad      = 4;
    static constexpr int menuBottomPad   = 4;
    static constexpr int rowTextPad      = 12;
};
