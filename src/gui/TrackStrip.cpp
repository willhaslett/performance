#include "gui/TrackStrip.h"
#include "api/PerformanceAPI.h"

TrackStrip::TrackStrip(const juce::String& id, const juce::String& name, PerformanceAPI& api)
    : api(api), trackId(id), trackName(name),
      instrumentSlot(PluginSlot::Instrument, api, id),
      sendsPanel(id, api) {

    addAndMakeVisible(instrumentSlot);
    addAndMakeVisible(faderMeter);
    addChildComponent(sendsPanel);  // hidden until busses exist

    faderMeter.onGainChanged = [&](float newGain) {
        api.setTrackGain(trackId, newGain);
    };

    instrumentSlot.onChanged = [this]() { rebuildEffectSlots(); };
}

void TrackStrip::setInstrumentName(const juce::String& name) {
    bool wasEmpty = !instrumentSlot.hasPlugin();
    instrumentSlot.setPluginName(name);
    if (wasEmpty && instrumentSlot.hasPlugin())
        rebuildEffectSlots();
}

void TrackStrip::setEffects(const std::vector<PerformanceAPI::EffectSlotInfo>& effects) {
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
            api.openPluginEditor(trackId, newFx.effectId);
        }
    }
}

void TrackStrip::setMidiEnabled(bool enabled) {
    if (midiEnabled != enabled) {
        midiEnabled = enabled;
        repaint();
    }
}

void TrackStrip::setPeakLevel(float level) {
    faderMeter.setPeakLevel(level);
}

void TrackStrip::setGain(float gain) {
    faderMeter.setGain(gain);
}

void TrackStrip::setSends(const std::vector<SendsPanel::SendInfo>& sends) {
    sendsPanel.setSends(sends);
}

void TrackStrip::setAvailableBusses(const std::vector<SendsPanel::BusOption>& busOptions) {
    sendsPanel.setAvailableBusses(busOptions);
    bool shouldShow = !busOptions.empty();
    if (shouldShow != sendsPanel.isVisible()) {
        sendsPanel.setVisible(shouldShow);
        resized();
    }
}

void TrackStrip::rebuildEffectSlots() {
    for (auto& slot : effectSlots)
        removeChildComponent(slot.get());
    effectSlots.clear();

    for (auto& fx : currentEffects) {
        auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, api, trackId);
        slot->setPluginName(fx.pluginName);
        slot->setEffectId(fx.effectId);
        addAndMakeVisible(*slot);
        effectSlots.push_back(std::move(slot));
    }

    if (instrumentSlot.hasPlugin()) {
        auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, api, trackId);
        slot->onChanged = [this]() { pendingEffectOpen = true; };
        addAndMakeVisible(*slot);
        effectSlots.push_back(std::move(slot));
    }

    resized();
}

void TrackStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    g.setColour(Theme::color(Theme::Color::bgTrack));
    g.fillRect(bounds);

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)bounds.getRight(), (float)bounds.getY(),
               (float)bounds.getRight(), (float)bounds.getBottom(), 1.0f);

    // Header
    headerBounds = juce::Rectangle<int>(bounds.getX(), bounds.getY(),
                                         bounds.getWidth(), Theme::headerHeight);
    g.setColour(Theme::color(midiEnabled ? Theme::Color::bgHeader : Theme::Color::bgHeaderOff));
    g.fillRect(headerBounds);

    // Power icon
    midiDotBounds = juce::Rectangle<int>(headerBounds.getX() + 6,
                                          headerBounds.getCentreY() - 7, 14, 14);
    auto iconColor = midiEnabled ? Theme::color(Theme::Color::midiActive)
                                  : Theme::color(Theme::Color::textDim);
    g.setColour(iconColor);
    juce::Path powerIcon;
    auto iconArea = midiDotBounds.reduced(1).toFloat();
    powerIcon.addCentredArc(iconArea.getCentreX(), iconArea.getCentreY(),
                             iconArea.getWidth() * 0.4f, iconArea.getHeight() * 0.4f,
                             0.0f, juce::MathConstants<float>::pi * 0.3f,
                             juce::MathConstants<float>::pi * 1.7f, true);
    g.strokePath(powerIcon, juce::PathStrokeType(1.5f));
    g.drawLine(iconArea.getCentreX(), iconArea.getY() + 1.0f,
               iconArea.getCentreX(), iconArea.getCentreY(), 1.5f);

    g.setColour(Theme::color(Theme::Color::textWhite));
    g.setFont(Theme::font(Theme::fontSize));
    g.drawText(trackName, headerBounds.withTrimmedLeft(26).withTrimmedRight(20).reduced(4, 0),
               juce::Justification::centredLeft);

    // Vertical dots menu button
    menuDotsBounds = juce::Rectangle<int>(headerBounds.getRight() - 18,
                                           headerBounds.getCentreY() - 7, 14, 14);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    auto dx = (float)menuDotsBounds.getCentreX();
    auto dy = (float)menuDotsBounds.getY() + 2.0f;
    for (int i = 0; i < 3; ++i) {
        g.fillEllipse(dx - 1.5f, dy, 3.0f, 3.0f);
        dy += 5.0f;
    }
}

int TrackStrip::getMinimumHeight() const {
    int h = Theme::headerHeight + Theme::trackPadding;
    // Instrument slot
    h += Theme::slotHeight + Theme::slotGap;
    // Effect slots
    h += (int)effectSlots.size() * (Theme::slotHeight + Theme::slotGap);
    // Sends
    if (sendsPanel.isVisible())
        h += sendsPanel.getDesiredHeight() + Theme::slotGap;
    h += Theme::trackPadding;  // bottom padding
    return h;
}

void TrackStrip::resized() {
    auto bounds = getLocalBounds();
    constexpr int faderMeterWidth = 28;

    // FaderMeter runs full height on the right
    auto fmArea = bounds.withTrimmedTop(Theme::headerHeight + Theme::trackPadding)
                        .withTrimmedBottom(Theme::trackPadding)
                        .removeFromRight(faderMeterWidth + Theme::trackPadding)
                        .withTrimmedRight(Theme::trackPadding);
    faderMeter.setBounds(fmArea);

    // Content area (left of fader)
    auto contentArea = bounds.withTrimmedTop(Theme::headerHeight)
                             .withTrimmedLeft(Theme::trackPadding)
                             .withTrimmedRight(faderMeterWidth + Theme::trackPadding * 2);

    // Sends panel at bottom of content area (dynamic height)
    if (sendsPanel.isVisible()) {
        int sendsHeight = sendsPanel.getDesiredHeight();
        sendsPanel.setBounds(contentArea.removeFromBottom(sendsHeight));
        contentArea.removeFromBottom(Theme::slotGap);
    }

    auto slotArea = contentArea.withTrimmedTop(Theme::trackPadding);
    int y = slotArea.getY();

    instrumentSlot.setBounds(slotArea.getX(), y, slotArea.getWidth(), Theme::slotHeight);
    y += Theme::slotHeight + Theme::slotGap;

    for (auto& slot : effectSlots) {
        slot->setBounds(slotArea.getX(), y, slotArea.getWidth(), Theme::slotHeight);
        y += Theme::slotHeight + Theme::slotGap;
    }
}

void TrackStrip::mouseUp(const juce::MouseEvent& event) {
    // Vertical dots menu
    if (menuDotsBounds.expanded(4).contains(event.getPosition())) {
        showTrackMenu(event.getScreenPosition());
        return;
    }

    // Right-click header also opens menu
    if (event.mods.isPopupMenu() && headerBounds.contains(event.getPosition())) {
        showTrackMenu(event.getScreenPosition());
        return;
    }

    auto midiHitArea = midiDotBounds.expanded(6);
    if (midiHitArea.contains(event.getPosition()) && !event.mods.isPopupMenu()) {
        midiEnabled = !midiEnabled;
        api.setTrackMidiEnabled(trackId, midiEnabled);
        repaint();
    }
}

void TrackStrip::showTrackMenu(juce::Point<int> screenPos) {
    juce::PopupMenu menu;
    menu.addItem(1, "Save Track Preset...");

    // Load submenu — only if presets exist
    auto presets = api.listTrackPresets();
    if (!presets.empty()) {
        juce::PopupMenu loadMenu;
        for (int i = 0; i < (int)presets.size(); ++i)
            loadMenu.addItem(100 + i, presets[i]);
        menu.addSubMenu("Load Track Preset", loadMenu);
    }

    menu.addSeparator();
    menu.addItem(10, "Delete Track");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, presets](int result) {
            if (result == 1) {
                juce::StringArray existing;
                for (auto& n : presets)
                    existing.add(n);
                SaveAsDialog::show("Save Track Preset", trackName, existing,
                    [this](const juce::String& name) {
                        api.saveTrackPreset(trackId, name);
                    });
            }
            else if (result >= 100 && result - 100 < (int)presets.size()) {
                api.loadTrackPreset(trackId, presets[result - 100]);
            }
            else if (result == 10) {
                api.removeTrack(trackId);
            }
        });
}

void TrackStrip::mouseDoubleClick(const juce::MouseEvent& event) {
    if (headerBounds.contains(event.getPosition())) {
        nameEditor.onCommit = [this](const juce::String& newName) {
            if (newName != trackName) {
                api.renameTrack(trackId, newName);
                trackName = newName;
                repaint();
            }
        };
        nameEditor.show(*this, headerBounds.withTrimmedLeft(26).reduced(4, 4), trackName);
    }
}
