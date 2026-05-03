#include "gui/BindableButton.h"
#include "api/PerformanceCoordinator.h"
#include <map>

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

juce::String BindableButton::slotDisplayText(const std::optional<BindingState>& b) const {
    if (!b) return "Trigger";
    // Resolve to the device's ControlDef name when we can — that's the
    // performer-friendly label ("Pad 1") rather than the wire details.
    auto* dev = state.findDevice(b->deviceId);
    if (dev) {
        for (auto& c : dev->controls) {
            if (c.controlType == b->controlType
                && c.channel == b->channel
                && c.number == b->number)
                return juce::String(c.name);
        }
    }
    // Fall back to the raw control coordinates if no matching ControlDef
    // (e.g. binding outlived the device's control list).
    return juce::String(b->controlType) + " " + juce::String(b->number);
}

juce::Rectangle<int> BindableButton::triggerSlotBounds() const {
    // Slot inset: small horizontal margin so the slot doesn't touch
    // the cell edges, fixed height at the bottom of the cell.
    constexpr int slotH = 18;
    constexpr int sidePad = 4;
    constexpr int botPad  = 4;
    auto b = getLocalBounds();
    return { b.getX() + sidePad,
              b.getBottom() - slotH - botPad,
              b.getWidth() - sidePad * 2,
              slotH };
}

void BindableButton::paintCellBackground(juce::Graphics& g, juce::Colour fill) {
    auto bounds = getLocalBounds().toFloat();
    const float r = 5.0f;
    juce::Path p;
    if (corners == Solo) {
        p.addRoundedRectangle(bounds, r);
    } else {
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

    // Cell background — darker than the legacy bgControl so the inner
    // trigger slot (bgControl) stands out as a distinct affordance.
    // Active = subtle inset (slightly darker still).
    juce::Colour bg = active ? Theme::color(Theme::Color::bgRowActive).darker(0.30f)
                              : Theme::color(Theme::Color::bgRowActive);
    paintCellBackground(g, bg);

    // Inner vertical divider on the right edge for non-rightmost cells —
    // gives the segmented strip cohesion (looks like one shape with
    // dividers, not a row of separate buttons).
    if (corners == Left || corners == Mid) {
        g.setColour(Theme::color(Theme::Color::borderSubtle));
        g.fillRect(getWidth() - 1, 4, 1, getHeight() - 8);
    }

    // Layout: label / icon on top, trigger slot at the bottom.
    auto slot = triggerSlotBounds();
    auto labelRow = getLocalBounds();
    labelRow.removeFromBottom(slot.getHeight() + 8);
    labelRow = labelRow.reduced(2, 4);

    // --- Row 1: label / icon ---
    juce::Colour textColor = enabled ? Theme::color(Theme::Color::textPrimary)
                                      : Theme::color(Theme::Color::textDim);
    if (variant == Variant::TextLabel) {
        g.setColour(textColor);
        g.setFont(Theme::font(Theme::fontSizeLg));
        g.drawText(label, labelRow, juce::Justification::centred);
    } else {
        int iconW = 18;
        auto icon = labelRow.withSizeKeepingCentre(iconW, iconW);
        paintIcon(g, icon, textColor);
    }

    // --- Row 2: trigger slot (plugin-slot-style) ---
    auto binding = findBindingForAction();
    bool bound = binding.has_value();
    g.setColour(Theme::color(slotHovered ? Theme::Color::bgControlHover
                                          : Theme::Color::bgControl));
    g.fillRoundedRectangle(slot.toFloat(), Theme::cornerRadiusSm);
    g.setFont(Theme::font(Theme::fontSizeSm));
    g.setColour(Theme::color(bound ? Theme::Color::accent
                                    : Theme::Color::textDim));
    g.drawText(slotDisplayText(binding), slot.reduced(6, 0),
               juce::Justification::centredLeft, true);

    // Whole-cell flash overlay on action fire — sits above everything
    // else (label, slot, divider) so the eye sees one unified pulse,
    // not "the dot lit up." Low alpha keeps the underlying content
    // readable through the flash.
    if (lit) {
        g.setColour(Theme::color(Theme::Color::triggerLight).withAlpha(0.35f));
        auto bounds = getLocalBounds().toFloat();
        const float r = 5.0f;
        juce::Path p;
        if (corners == Solo) {
            p.addRoundedRectangle(bounds, r);
        } else {
            bool tl = (corners == Left || corners == Solo);
            bool tr = (corners == Right || corners == Solo);
            bool bl = tl, br = tr;
            p.addRoundedRectangle(bounds.getX(), bounds.getY(),
                                  bounds.getWidth(), bounds.getHeight(),
                                  r, r, tl, tr, bl, br);
        }
        g.fillPath(p);
    }
}

void BindableButton::mouseDown(const juce::MouseEvent& e) {
    auto pos = e.getPosition();
    if (triggerSlotBounds().contains(pos)) {
        showTriggerMenu(e.getScreenPosition());
        return;
    }
    if (!isEnabled()) return;
    // Fire the action via the standard dispatch so all listeners (and
    // any algebra/Lua body) run. value=1 to count as a press.
    coord.executeAction(actionName.toStdString(), juce::var(), 1.0f);
}

void BindableButton::mouseMove(const juce::MouseEvent& e) {
    bool over = triggerSlotBounds().contains(e.getPosition());
    if (over != slotHovered) { slotHovered = over; repaint(); }
}

void BindableButton::mouseExit(const juce::MouseEvent&) {
    if (slotHovered) { slotHovered = false; repaint(); }
}

void BindableButton::showTriggerMenu(juce::Point<int> screenPos) {
    auto* song = state.currentSong();
    auto* myAction = state.findActionByName(actionName.toStdString());
    if (!song || !myAction) return;

    auto myBinding = findBindingForAction();
    auto allBindings = state.effectiveBindings();

    // (deviceId, ctype, channel, number) → binding owner. Used to gray
    // out controls that are already wired to a different action. Same
    // tuple matches SongRuntime's dispatch key, so disabling here is
    // exactly what would have fought us at runtime.
    auto isInUseByOther = [&](const std::string& deviceId, const std::string& ctype,
                                int ch, int num) -> bool {
        for (auto& b : allBindings) {
            if (b.actionId == myAction->id) continue;
            if (b.deviceId.str() == deviceId && b.controlType == ctype
                && b.channel == ch && b.number == num)
                return true;
        }
        return false;
    };
    auto isCurrent = [&](const std::string& deviceId, const std::string& ctype,
                          int ch, int num) -> bool {
        return myBinding && myBinding->deviceId.str() == deviceId
               && myBinding->controlType == ctype
               && myBinding->channel == ch && myBinding->number == num;
    };

    constexpr int kNoTriggerId = 90001;
    constexpr int kManageId    = 90002;

    juce::PopupMenu menu;

    if (myBinding) {
        menu.addItem(kNoTriggerId, "No Trigger");
        menu.addSeparator();
    }

    struct Key { DeviceId dev; std::string ctype; int ch; int num; };
    std::map<int, Key> idToControl;
    int idCursor = 1;

    auto& devices = state.allDevices();
    if (devices.empty()) {
        menu.addSectionHeader("No devices registered");
    } else {
        for (size_t di = 0; di < devices.size(); ++di) {
            auto& dev = devices[di];
            menu.addSectionHeader(juce::String(dev.name));
            for (auto& c : dev.controls) {
                int id = idCursor++;
                idToControl[id] = { dev.id, c.controlType, c.channel, c.number };

                bool current = isCurrent(dev.id.str(), c.controlType, c.channel, c.number);
                bool used = !current
                            && isInUseByOther(dev.id.str(), c.controlType, c.channel, c.number);

                juce::PopupMenu::Item item(juce::String(c.name)
                                            + (used ? " (in use)" : ""));
                item.itemID = id;
                item.isEnabled = !used;
                item.isTicked = current;
                menu.addItem(item);
            }
        }
    }

    menu.addSeparator();
    menu.addItem(kManageId, "Manage controls\xe2\x80\xa6");

    auto self = juce::Component::SafePointer<BindableButton>(this);
    auto songId = song->id;
    auto actionId = myAction->id;
    auto myBindingId = myBinding ? myBinding->id : BindingId{};
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [self, idToControl, songId, actionId, myBindingId](int result) {
            if (!self || result == 0) return;
            if (result == kManageId) {
                if (self->onManageControlsRequest) self->onManageControlsRequest();
                return;
            }
            // Single binding per action via this UI: nuke any existing
            // first, then add the new one (or none, if "No Trigger").
            if (! myBindingId.empty())
                self->state.removeBinding(myBindingId);
            if (result == kNoTriggerId) {
                self->repaint();
                return;
            }
            auto it = idToControl.find(result);
            if (it == idToControl.end()) return;
            auto& k = it->second;
            self->state.addBinding(songId, k.ctype, k.ch, k.num, actionId,
                                    "[]", "", k.dev);
            self->repaint();
        });
}
