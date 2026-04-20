#include "gui/ActionPicker.h"
#include "api/StateAPI.h"

ActionPicker::ActionPicker(StateAPI& s, Filter f)
    : state(s), filter(std::move(f)) {
    setWantsKeyboardFocus(true);
    rebuildFilteredActions();
    setSize(panelWidth, getDesiredHeight());
}

void ActionPicker::rebuildFilteredActions() {
    filteredActions.clear();
    actionEnabled.clear();
    for (auto& a : state.allActions()) {
        if (filter && !filter(a)) continue;
        filteredActions.push_back(a);
        actionEnabled.push_back(ActionInstanceForm::actionCanInstantiate(a, state));
    }
}

void ActionPicker::setInitialArgs(const juce::var& args) {
    if (mode == Mode::FillParams && form) form->setInitialArgs(args);
    else pendingInitialArgs = args;
}

void ActionPicker::setInitialAction(const ActionInfo& action) {
    enterFillMode(action);
}

void ActionPicker::enterFillMode(const ActionInfo& action) {
    mode = Mode::FillParams;
    form = std::make_unique<ActionInstanceForm>(state, action);
    if (!pendingInitialArgs.isVoid()) {
        form->setInitialArgs(pendingInitialArgs);
        pendingInitialArgs = juce::var();
    }
    form->onAccept = [this, action]() {
        if (onAccept) onAccept(action, form->getArgs());
    };
    // Defer back-to-pick: backToPickMode destroys the form and we're inside
    // its Cancel button click handler.
    form->onCancel = [this]() {
        juce::MessageManager::callAsync([this]() { backToPickMode(); });
    };
    addAndMakeVisible(*form);
    resizeWindowToContent();
    grabKeyboardFocus();
    repaint();
}

void ActionPicker::backToPickMode() {
    mode = Mode::PickAction;
    if (form) { removeChildComponent(form.get()); form.reset(); }
    hoveredRow = -1;
    resizeWindowToContent();
    grabKeyboardFocus();
    repaint();
}

bool ActionPicker::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (mode == Mode::FillParams) backToPickMode();
        else if (onDismiss) onDismiss();
        return true;
    }
    return false;
}

int ActionPicker::getDesiredHeight() const {
    if (mode == Mode::FillParams && form)
        return form->getDesiredHeight();
    return menuTopPad + (int)filteredActions.size() * actionRowHeight + menuBottomPad;
}

void ActionPicker::resizeWindowToContent() {
    setSize(panelWidth, getDesiredHeight());
}

int ActionPicker::rowAt(juce::Point<int> p) const {
    if (mode != Mode::PickAction) return -1;
    int y = p.y - menuTopPad;
    if (y < 0) return -1;
    int idx = y / actionRowHeight;
    if (idx < 0 || idx >= (int)filteredActions.size()) return -1;
    return idx;
}

void ActionPicker::mouseMove(const juce::MouseEvent& e) {
    int r = rowAt(e.getPosition());
    if (r >= 0 && !actionEnabled[(size_t)r]) r = -1;  // don't hover disabled rows
    if (r != hoveredRow) {
        hoveredRow = r;
        repaint();
    }
}

void ActionPicker::mouseExit(const juce::MouseEvent&) {
    if (hoveredRow != -1) {
        hoveredRow = -1;
        repaint();
    }
}

void ActionPicker::mouseUp(const juce::MouseEvent& e) {
    int r = rowAt(e.getPosition());
    if (r < 0 || !actionEnabled[(size_t)r]) return;
    enterFillMode(filteredActions[(size_t)r]);
}

void ActionPicker::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgOverlay));

    if (mode != Mode::PickAction) return;

    // List rows
    int y = menuTopPad;
    for (size_t i = 0; i < filteredActions.size(); ++i) {
        auto& a = filteredActions[i];
        bool enabled = actionEnabled[i];
        bool hovered = (int)i == hoveredRow;
        auto row = juce::Rectangle<int>(0, y, getWidth(), actionRowHeight);

        if (hovered) {
            g.setColour(Theme::color(Theme::Color::bgControlHover));
            g.fillRect(row);
        }

        auto textCol = enabled ? Theme::color(Theme::Color::textPrimary)
                                : Theme::color(Theme::Color::textDim);
        g.setColour(textCol);
        g.setFont(Theme::font(Theme::fontSizeMd));
        g.drawText(juce::String(a.label.empty() ? a.name : a.label),
                   row.reduced(rowTextPad, 0),
                   juce::Justification::centredLeft);

        y += actionRowHeight;
    }
}

void ActionPicker::resized() {
    if (mode == Mode::FillParams && form)
        form->setBounds(getLocalBounds());
}

// --- launch helpers ---

namespace {
// Borderless top-level popup: no title bar, no close button, drop-shadow.
// Same pattern our other context menus use.
constexpr int popupStyleFlags = juce::ComponentPeer::windowIsTemporary
                              | juce::ComponentPeer::windowHasDropShadow;

void positionAt(juce::Component& c, juce::Point<int> screenPos) {
    auto display = juce::Desktop::getInstance().getDisplays()
                    .getDisplayForPoint(screenPos);
    auto displayArea = display ? display->userArea
                                : juce::Rectangle<int>(0, 0, 1920, 1080);
    int x = std::min(screenPos.x, displayArea.getRight() - c.getWidth() - 8);
    int y = std::min(screenPos.y, displayArea.getBottom() - c.getHeight() - 8);
    x = std::max(x, displayArea.getX() + 8);
    y = std::max(y, displayArea.getY() + 8);
    c.setTopLeftPosition(x, y);
}
}  // namespace

void ActionPicker::launch(StateAPI& state, juce::Point<int> screenPos,
                           Filter filter,
                           std::function<void(const ActionInfo&, const juce::var&)> onAccept) {
    auto* picker = new ActionPicker(state, std::move(filter));
    picker->addToDesktop(popupStyleFlags);
    positionAt(*picker, screenPos);
    picker->setVisible(true);
    picker->setAlwaysOnTop(true);
    picker->grabKeyboardFocus();

    // Defer destruction to next message-loop tick — caller chains run through
    // nested lambdas (button click → form callback → picker callback → cleanup),
    // and synchronous deletion frees the lambdas mid-execution.
    auto deferredDelete = [picker]() {
        juce::MessageManager::callAsync([picker]() { delete picker; });
    };
    picker->onAccept = [deferredDelete, onAccept](const ActionInfo& a, const juce::var& args) {
        deferredDelete();
        if (onAccept) onAccept(a, args);
    };
    picker->onDismiss = [deferredDelete]() { deferredDelete(); };
}

void ActionPicker::launchEdit(StateAPI& state, juce::Point<int> screenPos,
                               const ActionInfo& action, const juce::var& initialArgs,
                               std::function<void(const ActionInfo&, const juce::var&)> onAccept) {
    auto* picker = new ActionPicker(state, {});
    if (!initialArgs.isVoid()) picker->setInitialArgs(initialArgs);
    picker->setInitialAction(action);

    picker->addToDesktop(popupStyleFlags);
    positionAt(*picker, screenPos);
    picker->setVisible(true);
    picker->setAlwaysOnTop(true);
    picker->grabKeyboardFocus();

    auto deferredDelete = [picker]() {
        juce::MessageManager::callAsync([picker]() { delete picker; });
    };
    picker->onAccept = [deferredDelete, onAccept](const ActionInfo& a, const juce::var& args) {
        deferredDelete();
        if (onAccept) onAccept(a, args);
    };
    picker->onDismiss = [deferredDelete]() { deferredDelete(); };
}
