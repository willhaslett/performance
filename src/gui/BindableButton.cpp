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

juce::Rectangle<int> BindableButton::bindingRowBounds() const {
    auto b = getLocalBounds();
    return b.removeFromBottom(18).reduced(4, 2);
}

juce::String BindableButton::currentBindingName() const {
    auto* a = state.findActionByName(actionName.toStdString());
    if (!a) return {};
    auto bindings = state.effectiveBindings();
    for (auto& b : bindings) {
        if (b.actionId != a->id) continue;
        if (!b.description.empty()) return juce::String(b.description);
        // Fallback: synthesize "<type> <ch> <num>"
        return juce::String(b.controlType) + " " + juce::String(b.number);
    }
    return {};
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

    // Cell background. Active wins over hover-flash. Disabled rows
    // get a faint, dimmer background.
    juce::Colour bg = active
                       ? Theme::color(Theme::Color::accent)
                       : Theme::color(Theme::Color::bgControl);
    paintCellBackground(g, bg);

    // --- Layer 1: label / icon (top ~half) ---
    auto contentArea = getLocalBounds().withTrimmedBottom(28);
    auto labelArea   = contentArea.withTrimmedBottom(8);  // leave room for activity light

    juce::Colour textColor = active     ? Theme::color(Theme::Color::textOnColor)
                              : enabled  ? Theme::color(Theme::Color::textPrimary)
                                          : Theme::color(Theme::Color::textDim);
    if (variant == Variant::TextLabel) {
        g.setColour(textColor);
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(label, labelArea, juce::Justification::centred);
    } else {
        // Icon centered in labelArea, sized to ~14px.
        int iconW = 14;
        auto icon = labelArea.withSizeKeepingCentre(iconW, iconW);
        paintIcon(g, icon, textColor);
    }

    // --- Layer 2: activity light (small dot, just above binding row) ---
    auto lightArea = juce::Rectangle<int>(0, contentArea.getBottom() - 8,
                                            getWidth(), 8);
    auto dot = lightArea.withSizeKeepingCentre(6, 6).toFloat();
    g.setColour(lit ? Theme::color(Theme::Color::accent)
                    : (active ? Theme::color(Theme::Color::textOnColor).withAlpha(0.35f)
                              : Theme::color(Theme::Color::bgRecessed)));
    g.fillEllipse(dot);

    // --- Layer 3: binding readout (bottom row) ---
    auto bindingRow = bindingRowBounds();
    auto bindingName = currentBindingName();
    g.setColour(active ? Theme::color(Theme::Color::textOnColor).withAlpha(0.85f)
                       : Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(Theme::fontSizeXs));
    g.drawText(bindingName.isEmpty() ? juce::String("+ set") : bindingName,
               bindingRow, juce::Justification::centred);
}

void BindableButton::mouseDown(const juce::MouseEvent& e) {
    auto pos = e.getPosition();
    if (bindingRowBounds().contains(pos)) {
        showSetControlPopup();
        return;
    }
    if (!isEnabled()) return;
    // Fire the action via the standard dispatch so all listeners (and
    // any algebra/Lua body) run. value=1 to count as a press.
    coord.executeAction(actionName.toStdString(), juce::var(), 1.0f);
}

void BindableButton::showSetControlPopup() {
    // Stub for now — the real MIDI-Learn-first popup arrives in the
    // next pass. Show a minimal menu so the surface is testable.
    juce::PopupMenu menu;
    auto bindingName = currentBindingName();
    if (bindingName.isNotEmpty())
        menu.addItem(1, "Bound to: " + bindingName, false);
    else
        menu.addItem(1, "(no control bound)", false);
    menu.addSeparator();
    menu.addItem(2, "Open Mappings\xe2\x80\xa6");  // ellipsis
    auto self = juce::Component::SafePointer<BindableButton>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [self](int result) {
            if (result == 2) {
                // Future: open Perform pane scrolled to this action.
                // For now, no-op — the user can navigate manually.
            }
            (void) self;
        });
}
