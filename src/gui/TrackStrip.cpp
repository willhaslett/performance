#include "gui/TrackStrip.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"

TrackStrip::TrackStrip(const juce::String& id, const juce::String& name,
                       StateAPI& state, EngineAPI& engine)
    : state(state), engine(engine), trackId(id), trackName(name),
      instrumentSlot(PluginSlot::Instrument, state, engine, id),
      sendsPanel(id, state) {

    addAndMakeVisible(instrumentSlot);
    addAndMakeVisible(faderMeter);
    addChildComponent(sendsPanel);  // hidden until busses exist

    faderMeter.onGainChanged = [&](float newGain) {
        state.setTrackGain(trackId.toStdString(), newGain);
    };

    instrumentSlot.onChanged = [this]() { rebuildEffectSlots(); };
}

void TrackStrip::setInstrumentName(const juce::String& name) {
    bool wasEmpty = !instrumentSlot.hasPlugin();
    instrumentSlot.setPluginName(name);
    if (wasEmpty && instrumentSlot.hasPlugin())
        rebuildEffectSlots();
}

void TrackStrip::setEffects(const std::vector<EffectSlotInfo>& effects) {
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
            engine.openPluginEditor(trackId, newFx.effectId);
        }
    }
}

void TrackStrip::setMidiEnabled(bool enabled) {
    if (midiEnabled != enabled) {
        midiEnabled = enabled;
        repaint();
    }
}

void TrackStrip::setAudioEnabled(bool enabled) {
    if (audioEnabled != enabled) {
        audioEnabled = enabled;
        repaint();
    }
}

void TrackStrip::setArmed(bool a) {
    if (armed != a) {
        armed = a;
        repaint();
    }
}

void TrackStrip::setPeakLevel(float level) {
    faderMeter.setPeakLevel(level);
}

void TrackStrip::setPeakLevelStereo(float left, float right) {
    faderMeter.setPeakLevelStereo(left, right);
}

void TrackStrip::setGain(float gain) {
    faderMeter.setGain(gain);
}

void TrackStrip::setSends(const std::vector<SendsPanel::SendInfo>& sends) {
    sendsPanel.setSends(sends);
}

void TrackStrip::setSourceType(TrackSourceType type) {
    if (sourceType == type) return;
    sourceType = type;
    if (type == TrackSourceType::AudioInput) {
        instrumentSlot.setVisible(false);
        rebuildEffectSlots();
    } else {
        instrumentSlot.setVisible(true);
    }
    resized();
    repaint();
}

void TrackStrip::setInputChannels(int start, int count, const std::vector<juce::String>& availableInputs) {
    cachedInputNames = availableInputs;
    inputChannelStart = start;
    inputChannelCount = count;

    if (start < 0 || count == 0) {
        inputDisplayName = {};
    } else if (count == 2) {
        inputDisplayName = "Input " + juce::String(start + 1) + "-" + juce::String(start + 2);
    } else {
        inputDisplayName = "Input " + juce::String(start + 1);
    }
    repaint();
}

void TrackStrip::setOutputTarget(const juce::String& target, const juce::String& displayName) {
    outputTargetId = target;
    outputTargetDisplay = displayName;
    repaint();
}

void TrackStrip::showOutputTargetMenu(juce::Point<int> screenPos) {
    juce::PopupMenu menu;
    menu.addItem(1, "Main", true, outputTargetId.isEmpty());
    menu.addItem(2, "No Output", true, outputTargetId == "none");

    // List available busses
    auto busOptions = sendsPanel.getAvailableBusses();
    for (int i = 0; i < (int)busOptions.size(); ++i) {
        menu.addItem(100 + i, busOptions[i].name,
                     true, outputTargetId == busOptions[i].id);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(screenPos.getX(), screenPos.getY(), 1, 1)),
        [this, busOptions](int result) {
            if (result == 1)
                state.setTrackOutputTarget(trackId.toStdString(), "");
            else if (result == 2)
                state.setTrackOutputTarget(trackId.toStdString(), "none");
            else if (result >= 100) {
                int idx = result - 100;
                if (idx < (int)busOptions.size())
                    state.setTrackOutputTarget(trackId.toStdString(), busOptions[idx].id.toStdString());
            }
        });
}

void TrackStrip::paintInputSlot(juce::Graphics& g) {
    g.setColour(Theme::color(inputSlotHovered ? Theme::Color::bgSlotHover : Theme::Color::bgSlot));
    g.fillRoundedRectangle(inputSlotBounds.toFloat(), Theme::cornerRadiusSm);

    g.setFont(Theme::font(Theme::fontSizeSm));
    if (inputDisplayName.isEmpty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.drawText("Select Input", inputSlotBounds.reduced(8, 0), juce::Justification::centredLeft);
    } else {
        g.setColour(Theme::color(Theme::Color::instrument));
        g.drawText(inputDisplayName, inputSlotBounds.reduced(8, 0), juce::Justification::centredLeft);
    }
}

void TrackStrip::showInputPicker(juce::Point<int> screenPos) {
    juce::PopupMenu menu;
    menu.addItem(1, "No Input", true, inputChannelStart < 0);

    int numInputs = (int)cachedInputNames.size();

    if (numInputs > 0) {
        menu.addSeparator();
        // Mono inputs
        for (int i = 0; i < numInputs; ++i) {
            juce::String label = cachedInputNames[i].isEmpty()
                ? "Input " + juce::String(i + 1)
                : cachedInputNames[i];
            bool isCurrent = (inputChannelStart == i && inputChannelCount == 1);
            menu.addItem(i + 2, label, true, isCurrent);
        }

        // Stereo pairs
        if (numInputs >= 2) {
            menu.addSeparator();
            for (int i = 0; i + 1 < numInputs; i += 2) {
                juce::String label = "Input " + juce::String(i + 1) + "-" + juce::String(i + 2) + " (Stereo)";
                bool isCurrent = (inputChannelStart == i && inputChannelCount == 2);
                menu.addItem(1000 + i + 2, label, true, isCurrent);
            }
        }
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
        .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this](int result) {
            if (result == 0) return;
            if (result == 1) {
                state.setTrackInputChannels(trackId.toStdString(), -1, 0);
            } else if (result >= 1000) {
                state.setTrackInputChannels(trackId.toStdString(), result - 1002, 2);
            } else {
                state.setTrackInputChannels(trackId.toStdString(), result - 2, 1);
            }
        });
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
        auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, state, engine, trackId);
        slot->setPluginName(fx.pluginName);
        slot->setEffectId(fx.effectId);
        addAndMakeVisible(*slot);
        effectSlots.push_back(std::move(slot));
    }

    // Show empty "add effect" slot for instrument tracks with a plugin, or any audio input track
    if (instrumentSlot.hasPlugin() || sourceType == TrackSourceType::AudioInput) {
        auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, state, engine, trackId);
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

    bool isAudioInput = (sourceType == TrackSourceType::AudioInput);
    constexpr uint32_t bgHeaderAudioInput = 0xff3a2e18;  // muted amber
    constexpr float disabledDarken = 0.35f;  // blend toward black

    auto headerColour = isAudioInput ? juce::Colour(bgHeaderAudioInput)
                                      : Theme::color(Theme::Color::bgHeader);
    if (!audioEnabled)
        headerColour = headerColour.interpolatedWith(juce::Colour(0xff181818), 1.0f - disabledDarken);
    g.setColour(headerColour);
    g.fillRect(headerBounds);

    // Layout: 8px | power(14) | 6px | arm(12) | 6px | track name | ... | menu
    int cx = headerBounds.getX() + 8;
    int cy = headerBounds.getCentreY();

    midiDotBounds = juce::Rectangle<int>(cx, cy - 7, 14, 14);

    {
        auto iconColor = audioEnabled ? Theme::color(Theme::Color::textWhite)
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
    }
    cx += 14 + 6;

    // Record arm dot — only shown when track is enabled
    armDotBounds = juce::Rectangle<int>(cx, cy - 6, 12, 12);
    if (audioEnabled) {
        if (armed) {
            g.setColour(juce::Colour(0xffee8822));
            g.fillEllipse(armDotBounds.toFloat());
        } else {
            g.setColour(Theme::color(Theme::Color::textDim));
            g.drawEllipse(armDotBounds.reduced(1).toFloat(), 1.5f);
        }
    }
    cx += 12 + 6;

    g.setColour(audioEnabled ? Theme::color(Theme::Color::textWhite)
                              : Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(Theme::fontSize));
    g.drawText(trackName, headerBounds.withTrimmedLeft(cx - headerBounds.getX()).withTrimmedRight(20),
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

    // Input slot for audio input tracks
    if (sourceType == TrackSourceType::AudioInput)
        paintInputSlot(g);

    // Output target label (bottom of strip, above fader)
    if (outputTargetBounds.getHeight() > 0) {
        g.setColour(Theme::color(Theme::Color::bgSlot));
        g.fillRoundedRectangle(outputTargetBounds.toFloat(), Theme::cornerRadiusSm);
        g.setFont(Theme::font(9.0f));
        g.setColour(Theme::color(Theme::Color::textSecondary));
        auto label = outputTargetDisplay.isEmpty() ? "Main" : outputTargetDisplay;
        g.drawText(label, outputTargetBounds.reduced(4, 0), juce::Justification::centredLeft);
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
    constexpr int faderMeterWidth = 48;

    // FaderMeter runs full height on the right
    auto fmArea = bounds.withTrimmedTop(Theme::headerHeight + Theme::trackPadding)
                        .withTrimmedBottom(Theme::trackPadding)
                        .removeFromRight(faderMeterWidth + Theme::trackPadding)
                        .withTrimmedRight(Theme::trackPadding);
    faderMeter.setBounds(fmArea);

    // Content area (left of fader)
    auto contentArea = bounds.withTrimmedTop(Theme::headerHeight)
                             .withTrimmedLeft(Theme::trackPadding)
                             .withTrimmedRight(faderMeterWidth + Theme::trackPadding * 2)
                             .withTrimmedBottom(Theme::trackPadding);

    // Output target pinned to bottom
    outputTargetBounds = contentArea.removeFromBottom(18);
    contentArea.removeFromBottom(2);

    // Slots at top, then sends below with padding
    int y = contentArea.getY() + Theme::trackPadding;

    instrumentSlot.setBounds(contentArea.getX(), y, contentArea.getWidth(), Theme::slotHeight);
    inputSlotBounds = juce::Rectangle<int>(contentArea.getX(), y, contentArea.getWidth(), Theme::slotHeight);
    y += Theme::slotHeight + Theme::slotGap;

    for (auto& slot : effectSlots) {
        slot->setBounds(contentArea.getX(), y, contentArea.getWidth(), Theme::slotHeight);
        y += Theme::slotHeight + Theme::slotGap;
    }

    // Sends panel top-aligned after slots with generous padding
    if (sendsPanel.isVisible()) {
        y += Theme::slotGap * 3;  // generous gap to visually separate sends from slots
        int sendsHeight = sendsPanel.getDesiredHeight();
        sendsPanel.setBounds(contentArea.getX(), y, contentArea.getWidth(), sendsHeight);
    }
}

void TrackStrip::mouseDown(const juce::MouseEvent& event) {
    // Track header drag initiation
    if (headerBounds.contains(event.getPosition()) && !event.mods.isPopupMenu())
        dragStarted = false;  // will start on mouseDrag if moved enough
}

void TrackStrip::mouseDrag(const juce::MouseEvent& event) {
    if (headerBounds.contains(event.getMouseDownPosition())
        && event.getDistanceFromDragStart() > 5 && !event.mods.isPopupMenu()) {
        if (!dragStarted) {
            dragStarted = true;
            if (onDragStart) onDragStart(trackId, getX());
        }
        if (onDragMove) {
            auto screenPos = event.getEventRelativeTo(getParentComponent()).getPosition();
            onDragMove(screenPos.getX());
        }
    }
}

void TrackStrip::mouseUp(const juce::MouseEvent& event) {
    if (dragStarted) {
        dragStarted = false;
        if (onDragEnd) onDragEnd();
        return;
    }
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

    // Arm dot toggles recording arm (only when track is enabled)
    if (audioEnabled && armDotBounds.expanded(4).contains(event.getPosition()) && !event.mods.isPopupMenu()) {
        armed = !armed;
        state.setTrackArmed(trackId.toStdString(), armed);
        repaint();
        return;
    }

    // Power icon toggles audioEnabled (all track types)
    auto powerHitArea = midiDotBounds.expanded(6);
    if (powerHitArea.contains(event.getPosition()) && !event.mods.isPopupMenu()) {
        audioEnabled = !audioEnabled;
        state.setTrackAudioEnabled(trackId.toStdString(), audioEnabled);
        if (audioEnabled && sourceType == TrackSourceType::Instrument)
            state.setTrackMidiEnabled(trackId.toStdString(), true);
        if (!audioEnabled && armed) {
            armed = false;
            state.setTrackArmed(trackId.toStdString(), false);
        }
        repaint();
        return;
    }

    // Output target click
    if (outputTargetBounds.contains(event.getPosition()) && !event.mods.isPopupMenu()) {
        showOutputTargetMenu(event.getScreenPosition());
        return;
    }

    // Input slot click — show input picker
    if (sourceType == TrackSourceType::AudioInput &&
        inputSlotBounds.contains(event.getPosition()) && !event.mods.isPopupMenu()) {
        showInputPicker(event.getScreenPosition());
    }
}

void TrackStrip::showTrackMenu(juce::Point<int> screenPos) {
    juce::PopupMenu menu;
    menu.addItem(1, "Save Track Preset...");

    // Load submenu — only if presets exist
    std::vector<juce::String> presets;
    if (onListTrackPresets)
        presets = onListTrackPresets();

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
                        if (onSaveTrackPreset)
                            onSaveTrackPreset(trackId, name);
                    });
            }
            else if (result >= 100 && result - 100 < (int)presets.size()) {
                if (onLoadTrackPreset)
                    onLoadTrackPreset(trackId, presets[result - 100]);
            }
            else if (result == 10) {
                state.removeTrack(trackId.toStdString());
            }
        });
}

void TrackStrip::mouseDoubleClick(const juce::MouseEvent& event) {
    if (headerBounds.contains(event.getPosition())) {
        nameEditor.onCommit = [this](const juce::String& newName) {
            if (newName != trackName) {
                state.renameTrack(trackId.toStdString(), newName.toStdString());
                trackName = newName;
                repaint();
            }
        };
        nameEditor.show(*this, headerBounds.withTrimmedLeft(26).reduced(4, 4), trackName);
    }
}
