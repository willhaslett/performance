#include "gui/BindableButton.h"
#include "api/PerformanceCoordinator.h"

BindableButton::BindableButton(StateAPI& s,
                                 PerformanceCoordinator& c,
                                 juce::String action,
                                 juce::String lbl,
                                 Variant v)
    : state(s), coord(c), actionName(std::move(action)),
      label(std::move(lbl)), variant(v) {
    setOpaque(false);
    // Subscribe to action-fire events. Flash on this action only;
    // use a SafePointer so a fire that arrives during teardown
    // doesn't dereference us.
    auto self = juce::Component::SafePointer<BindableButton>(this);
    actionFireSubId = coord.addActionFireListener(
        [self, name = actionName](const std::string& fired) {
            if (fired != name.toStdString()) return;
            if (!self) return;
            // Trigger from anywhere — defer to message thread for the
            // repaint + clear timer, since fires may originate on
            // other threads.
            juce::MessageManager::callAsync([self]() {
                if (!self) return;
                self->litUntilMs = juce::Time::currentTimeMillis() + 200;
                self->repaint();
                juce::Timer::callAfterDelay(220, [self]() {
                    if (self) self->repaint();
                });
            });
        });
}

BindableButton::~BindableButton() {
    if (actionFireSubId >= 0)
        coord.removeActionFireListener(actionFireSubId);
}

bool BindableButton::isLit() const {
    return juce::Time::currentTimeMillis() < litUntilMs;
}

std::optional<BindingState> BindableButton::findBindingForAction() const {
    auto* a = state.findActionByName(actionName.toStdString());
    if (!a) return std::nullopt;
    auto bindings = state.effectiveBindings();
    for (auto& b : bindings)
        if (b.actionId == a->id)
            return b;
    return std::nullopt;
}

void BindableButton::paintCellBackground(juce::Graphics& g, juce::Colour fill) {
    auto bounds = getLocalBounds().toFloat();
    const float r = 5.0f;
    juce::Path p;
    if (corners == Solo) {
        p.addRoundedRectangle(bounds, r);
    } else {
        // Per-corner rounding: outer corners only.
        bool tl = (corners == Left || corners == Solo);
        bool tr = (corners == Right || corners == Solo);
        bool bl = tl, br = tr;
        p.addRoundedRectangle(bounds.getX(), bounds.getY(),
                              bounds.getWidth(), bounds.getHeight(),
                              r, r, tl, tr, bl, br);
    }
    g.setColour(fill);
    g.fillPath(p);
}

void BindableButton::paintIcon(juce::Graphics& g, juce::Rectangle<int> area,
                                 juce::Colour col) {
    g.setColour(col);
    auto inner = area.toFloat().reduced(3.0f);
    switch (variant) {
        case Variant::IconPlay: {
            // Show play if stopped, square stop if playing — caller
            // toggles activePred to indicate playing state.
            if (isActive()) {
                g.fillRect(inner.reduced(2.0f));
            } else {
                juce::Path tri;
                tri.addTriangle(inner.getX(), inner.getY(),
                                inner.getX(), inner.getBottom(),
                                inner.getRight(), inner.getCentreY());
                g.fillPath(tri);
            }
            break;
        }
        case Variant::IconArrowUp: {
            juce::Path tri;
            tri.addTriangle(inner.getCentreX(), inner.getY(),
                            inner.getX(), inner.getBottom(),
                            inner.getRight(), inner.getBottom());
            g.fillPath(tri);
            break;
        }
        case Variant::IconArrowDown: {
            juce::Path tri;
            tri.addTriangle(inner.getX(), inner.getY(),
                            inner.getRight(), inner.getY(),
                            inner.getCentreX(), inner.getBottom());
            g.fillPath(tri);
            break;
        }
        case Variant::TextLabel:
            // Drawn as text in paint(); icon path unused.
            break;
    }
}

void BindableButton::paint(juce::Graphics& g) {
    bool active = isActive();
    bool enabled = isEnabled();
    bool lit = isLit();
    bool learnMode = isLearnMode();
    bool armed = isArmed();

    // Cell background. In learn mode we subtly shift to bgSlot so the
    // whole strip reads as "armable surface, not action surface" without
    // blowing out the visual. Active = subtle inset (slightly darker),
    // not the big accent fill.
    juce::Colour bg;
    if (learnMode) {
        bg = Theme::color(Theme::Color::bgSlot);
    } else if (active) {
        bg = Theme::color(Theme::Color::bgControl).darker(0.25f);
    } else {
        bg = Theme::color(Theme::Color::bgControl);
    }
    paintCellBackground(g, bg);

    // Optional top color stripe — category hint (e.g. Replace/Overdub
    // tints that match their lane-state colors).
    if (! topStripe.isTransparent()) {
        g.setColour(topStripe);
        g.fillRect(0, 0, getWidth(), 3);
    }

    // Inner vertical divider on the right edge for non-rightmost cells —
    // gives the segmented strip cohesion (looks like one shape with
    // dividers, not a row of separate buttons).
    if (corners == Left || corners == Mid) {
        g.setColour(Theme::color(Theme::Color::borderSubtle));
        g.fillRect(getWidth() - 1, 4, 1, getHeight() - 8);
    }

    // Two rows top-to-bottom: label / status dot. Dot row is fixed
    // height; label takes the rest. Removing the binding-name row
    // bought us breathing room on label legibility.
    constexpr int dotRowH = 12;
    auto area = getLocalBounds().reduced(2, 4);
    auto dotRow   = area.removeFromBottom(dotRowH);
    auto labelRow = area;  // remainder = top

    // --- Row 1: label / icon ---
    juce::Colour textColor = enabled ? Theme::color(Theme::Color::textPrimary)
                                      : Theme::color(Theme::Color::textDim);
    if (variant == Variant::TextLabel) {
        // Optional record-dot left of the label (Replace/Overdub).
        auto labelDraw = labelRow;
        if (showRecordDot) {
            int dotSize = 8;
            auto dotArea = labelDraw.removeFromLeft(dotSize + 8);
            auto dot = dotArea.withSizeKeepingCentre(dotSize, dotSize).toFloat();
            g.setColour(Theme::color(Theme::Color::transportRecDot));
            g.fillEllipse(dot);
        }
        g.setColour(textColor);
        g.setFont(Theme::font(Theme::fontSizeLg));
        g.drawText(label, labelDraw, juce::Justification::centred);
    } else {
        int iconW = 18;
        auto icon = labelRow.withSizeKeepingCentre(iconW, iconW);
        paintIcon(g, icon, textColor);
    }

    // --- Row 2: status dot (binding state + activity flash) ---
    // Priority: armed > lit (yellow flash) > bound > unbound. Armed
    // wins so the user always sees which cell will catch the next
    // MIDI event during learn.
    bool bound = findBindingForAction().has_value();
    juce::Colour dotColor;
    if (armed)      dotColor = Theme::color(Theme::Color::bindingDotArmed);
    else if (lit)   dotColor = Theme::color(Theme::Color::triggerLight);
    else if (bound) dotColor = Theme::color(Theme::Color::bindingDotBound);
    else            dotColor = Theme::color(Theme::Color::bindingDotUnbound);

    int dotSize = 8;
    auto trigDot = dotRow.withSizeKeepingCentre(dotSize, dotSize).toFloat();
    g.setColour(dotColor);
    g.fillEllipse(trigDot);
}

void BindableButton::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        showContextMenu();
        return;
    }
    if (isLearnMode()) {
        if (onArmRequest) onArmRequest();
        return;
    }
    if (!isEnabled()) return;
    // Fire the action via the standard dispatch so all listeners (and
    // any algebra/Lua body) run. value=1 to count as a press.
    coord.executeAction(actionName.toStdString(), juce::var(), 1.0f);
}

void BindableButton::showContextMenu() {
    auto binding = findBindingForAction();
    juce::PopupMenu menu;
    if (binding) {
        juce::String name = binding->description.empty()
            ? juce::String(binding->controlType) + " " + juce::String(binding->number)
            : juce::String(binding->description);
        menu.addItem(1, "Bound to: " + name, false);
        menu.addSeparator();
        menu.addItem(2, "Re-bind\xe2\x80\xa6");
        menu.addItem(3, "Unbind");
    } else {
        menu.addItem(2, "Bind\xe2\x80\xa6");
    }
    menu.addSeparator();
    menu.addItem(4, "Open Mappings\xe2\x80\xa6");

    auto self = juce::Component::SafePointer<BindableButton>(this);
    auto bindingId = binding ? binding->id : BindingId{};
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [self, bindingId](int result) {
            if (!self) return;
            switch (result) {
                case 2:
                    // Bind / Re-bind: arm via the host pane's learn flow
                    // if it provided one. Otherwise no-op (host pane
                    // hasn't opted into learn yet).
                    if (self->onArmRequest) self->onArmRequest();
                    break;
                case 3:
                    if (! bindingId.empty())
                        self->state.removeBinding(bindingId);
                    self->repaint();
                    break;
                case 4:
                    // Future: open Perform pane scrolled to this action.
                    break;
                default: break;
            }
        });
}
