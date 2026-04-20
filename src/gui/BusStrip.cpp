#include "gui/BusStrip.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "state/ActionRefs.h"

BusStrip::BusStrip(const juce::String& id, const juce::String& name,
                   StateAPI& state, EngineAPI& engine)
    : state(state), engine(engine), busId(id), busName(name) {

    addAndMakeVisible(faderMeter);

    faderMeter.onGainChanged = [&](float newGain) {
        state.setBusGain(BusId{busId.toStdString()}, newGain);
    };
    faderMeter.onDragStart = [&]() { state.beginTransaction(); };
    faderMeter.onDragEnd = [&]() { state.endTransaction(); };

    // Start with one empty effect slot
    rebuildEffectSlots();
}

void BusStrip::confirmAndRemoveBus(const BusId& id) {
    auto deps = ActionRefs::countDependents(state, id.str());
    if (deps.actionEvents == 0 && deps.bindings == 0) {
        state.removeBus(id);
        return;
    }

    auto* bus = state.findBus(id);
    juce::String name = bus ? juce::String(bus->name) : juce::String("bus");

    juce::String parts;
    if (deps.actionEvents > 0)
        parts << deps.actionEvents << " action event" << (deps.actionEvents == 1 ? "" : "s");
    if (deps.bindings > 0) {
        if (parts.isNotEmpty()) parts << " and ";
        parts << deps.bindings << " MIDI binding" << (deps.bindings == 1 ? "" : "s");
    }

    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Delete bus",
        "Deleting \"" + name + "\" will also remove " + parts + " that reference it.",
        "Delete", "Cancel", nullptr,
        juce::ModalCallbackFunction::create([this, id](int ok) {
            if (ok == 1) {
                ActionRefs::removeDependents(state, id.str());
                state.removeBus(id);
            }
        }));
}

void BusStrip::setEffects(const std::vector<EffectSlotInfo>& effects) {
    bool changed = (effects.size() != currentEffects.size());
    if (!changed) {
        for (size_t i = 0; i < effects.size(); ++i) {
            if (effects[i].effectId != currentEffects[i].effectId ||
                effects[i].pluginName != currentEffects[i].pluginName) {
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        bool effectAdded = pendingEffectOpen && effects.size() > currentEffects.size();
        currentEffects = effects;
        rebuildEffectSlots();

        if (effectAdded && !currentEffects.empty()) {
            pendingEffectOpen = false;
            auto& newFx = currentEffects.back();
            engine.openPluginEditor(busId, newFx.effectId);
        }
    }
}

void BusStrip::setPeakLevel(float level) {
    faderMeter.setPeakLevel(level);
}

void BusStrip::setPeakLevelStereo(float left, float right) {
    faderMeter.setPeakLevelStereo(left, right);
}

void BusStrip::setGain(float gain) {
    faderMeter.setGain(gain);
}

void BusStrip::rebuildEffectSlots() {
    for (auto& slot : effectSlots)
        removeChildComponent(slot.get());
    effectSlots.clear();

    for (auto& fx : currentEffects) {
        auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, state, engine, busId, PluginSlot::OnBus);
        slot->setPluginName(fx.pluginName);
        slot->setEffectId(fx.effectId);
        addAndMakeVisible(*slot);
        effectSlots.push_back(std::move(slot));
    }

    // Always an empty slot for adding new effect
    auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, state, engine, busId, PluginSlot::OnBus);
    slot->onChanged = [this]() { pendingEffectOpen = true; };
    addAndMakeVisible(*slot);
    effectSlots.push_back(std::move(slot));

    resized();
}

void BusStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    g.setColour(Theme::color(Theme::Color::bgSurface));
    g.fillRect(bounds);

    // Header
    headerBounds = juce::Rectangle<int>(bounds.getX(), bounds.getY(),
                                         bounds.getWidth(), Theme::headerHeight);
    g.setColour(Theme::color(Theme::Color::bgSurface));
    g.fillRect(headerBounds);

    // Type accent — 2px top stripe (bus).
    g.setColour(Theme::color(Theme::Color::typeBus));
    g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 2);

    g.setColour(Theme::color(Theme::Color::textOnColor));
    g.setFont(Theme::font(Theme::fontSizeLg));
    g.drawText(busName, headerBounds.reduced(Theme::spacingM, 0),
               juce::Justification::centredLeft);

    // Output target label
    if (outputTargetBounds.getHeight() > 0) {
        g.setColour(Theme::color(Theme::Color::bgControl));
        g.fillRoundedRectangle(outputTargetBounds.toFloat(), Theme::cornerRadiusSm);
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.setColour(Theme::color(Theme::Color::textSecondary));
        auto label = outputTargetDisplay.isEmpty() ? "Main" : outputTargetDisplay;
        g.drawText(label, outputTargetBounds.reduced(4, 0), juce::Justification::centredLeft);
    }

    // Left-edge border — drawn last
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)bounds.getX(), (float)bounds.getY(),
               (float)bounds.getX(), (float)bounds.getBottom(), 1.0f);
}

int BusStrip::getMinimumHeight() const {
    int h = Theme::headerHeight + Theme::trackPadding;
    h += (int)effectSlots.size() * (Theme::slotHeight + Theme::slotGap);
    h += Theme::trackPadding;
    return h;
}

void BusStrip::setOutputTarget(const juce::String& target, const juce::String& displayName) {
    outputTargetId = target;
    outputTargetDisplay = displayName;
    repaint();
}

void BusStrip::mouseUp(const juce::MouseEvent& event) {
    // Output target click
    if (outputTargetBounds.contains(event.getPosition()) && !event.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addItem(1, "Main", true, outputTargetId.isEmpty());
        menu.addItem(2, "No Output", true, outputTargetId == "none");
        // List other busses for bus-to-bus routing
        auto allBusses = state.listBusses();
        for (int i = 0; i < (int)allBusses.size(); ++i) {
            if (juce::String(allBusses[i].id.str()) == busId) continue;  // skip self
            menu.addItem(100 + i, juce::String(allBusses[i].name),
                         true, outputTargetId == juce::String(allBusses[i].id.str()));
        }
        auto bId = busId;
        auto busListCopy = allBusses;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
            [this, bId, busListCopy](int result) {
                if (result == 1)
                    state.setBusOutputTarget(BusId{bId.toStdString()}, "");
                else if (result == 2)
                    state.setBusOutputTarget(BusId{bId.toStdString()}, "none");
                else if (result >= 100) {
                    int idx = result - 100;
                    if (idx < (int)busListCopy.size())
                        state.setBusOutputTarget(BusId{bId.toStdString()}, busListCopy[idx].id.str());
                }
            });
        return;
    }

    if (event.mods.isPopupMenu() && headerBounds.contains(event.getPosition())) {
        juce::PopupMenu menu;

        // Preset submenu
        juce::PopupMenu presetMenu;
        presetMenu.addItem(10, "Save Bus Preset...");
        if (onListBusPresets) {
            auto presets = onListBusPresets();
            if (!presets.empty()) {
                presetMenu.addSeparator();
                for (int i = 0; i < (int)presets.size(); ++i)
                    presetMenu.addItem(100 + i, "Load: " + presets[i]);
            }
        }
        menu.addSubMenu("Presets", presetMenu);
        menu.addSeparator();
        menu.addItem(1, "Delete Bus");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
            [this](int result) {
                if (result == 1)
                    confirmAndRemoveBus(BusId{busId.toStdString()});
                else if (result == 10 && onSaveBusPreset) {
                    auto* dlg = new juce::AlertWindow("Save Bus Preset", "", juce::MessageBoxIconType::NoIcon);
                    dlg->addTextEditor("name", busName, "Preset Name");
                    dlg->addButton("Save", 1);
                    dlg->addButton("Cancel", 0);
                    auto id = busId;
                    auto saveCb = onSaveBusPreset;
                    dlg->enterModalState(true, juce::ModalCallbackFunction::create(
                        [dlg, id, saveCb](int r) {
                            if (r == 1) saveCb(id, dlg->getTextEditorContents("name"));
                            delete dlg;
                        }));
                }
                else if (result >= 100 && onLoadBusPreset && onListBusPresets) {
                    auto presets = onListBusPresets();
                    int idx = result - 100;
                    if (idx < (int)presets.size())
                        onLoadBusPreset(busId, presets[idx]);
                }
            });
    }
}

void BusStrip::mouseDoubleClick(const juce::MouseEvent& event) {
    if (headerBounds.contains(event.getPosition())) {
        nameEditor.onCommit = [this](const juce::String& newName) {
            if (newName != busName) {
                state.renameBus(BusId{busId.toStdString()}, newName.toStdString());
                busName = newName;
                repaint();
            }
        };
        nameEditor.show(*this, headerBounds.reduced(8, 4), busName);
    }
}

void BusStrip::resized() {
    auto bounds = getLocalBounds();
    constexpr int faderMeterWidth = 48;

    auto fmArea = bounds.withTrimmedTop(Theme::headerHeight + Theme::trackPadding)
                        .withTrimmedBottom(Theme::trackPadding)
                        .removeFromRight(faderMeterWidth + Theme::trackPadding)
                        .withTrimmedRight(Theme::trackPadding);
    faderMeter.setBounds(fmArea);

    auto contentArea = bounds.withTrimmedTop(Theme::headerHeight + Theme::trackPadding)
                              .withTrimmedLeft(Theme::trackPadding)
                              .withTrimmedRight(faderMeterWidth + Theme::trackPadding * 2)
                              .withTrimmedBottom(Theme::trackPadding);

    // Output target at the very bottom
    outputTargetBounds = contentArea.removeFromBottom(18);
    contentArea.removeFromBottom(2);

    // Effect slots at top
    int y = contentArea.getY();
    for (auto& slot : effectSlots) {
        slot->setBounds(contentArea.getX(), y, contentArea.getWidth(), Theme::slotHeight);
        y += Theme::slotHeight + Theme::slotGap;
    }
}
