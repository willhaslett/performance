#include "gui/OutputStrip.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"

OutputStrip::OutputStrip(StateAPI& state, EngineAPI& engine)
    : state(state), engine(engine) {
    addAndMakeVisible(faderMeter);

    faderMeter.onGainChanged = [&](float newGain) {
        state.setMasterGain(newGain);
    };
    // Don't rebuild slots in constructor — getMasterOutputId() may be empty.
    // First rebuild happens when MixerView calls setEffects() after session restore.
}

void OutputStrip::setEffects(const std::vector<EffectSlotInfo>& effects) {
    bool changed = effectSlots.empty() || (effects.size() != currentEffects.size());
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
            auto masterOutputId = juce::String(state.getMasterOutputId());
            engine.openPluginEditor(masterOutputId, newFx.effectId);
        }
    }
}

void OutputStrip::setPeakLevel(float level) {
    faderMeter.setPeakLevel(level);
}

void OutputStrip::setPeakLevelStereo(float left, float right) {
    faderMeter.setPeakLevelStereo(left, right);
}

void OutputStrip::setGain(float gain) {
    faderMeter.setGain(gain);
}

int OutputStrip::getMinimumHeight() const {
    int h = Theme::headerHeight + Theme::trackPadding;
    h += (int)effectSlots.size() * (Theme::slotHeight + Theme::slotGap);
    h += Theme::trackPadding;
    return h;
}

void OutputStrip::rebuildEffectSlots() {
    for (auto& slot : effectSlots)
        removeChildComponent(slot.get());
    effectSlots.clear();

    auto masterOutputId = juce::String(state.getMasterOutputId());
    for (auto& fx : currentEffects) {
        auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, state, engine, masterOutputId, PluginSlot::OnBus);
        slot->setPluginName(fx.pluginName);
        slot->setEffectId(fx.effectId);
        addAndMakeVisible(*slot);
        effectSlots.push_back(std::move(slot));
    }

    // Always an empty slot for adding new effect
    auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, state, engine, masterOutputId, PluginSlot::OnBus);
    slot->onChanged = [this]() { pendingEffectOpen = true; };
    addAndMakeVisible(*slot);
    effectSlots.push_back(std::move(slot));

    resized();
}

void OutputStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    g.setColour(Theme::color(Theme::Color::bgTrack));
    g.fillRect(bounds);

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)bounds.getX(), (float)bounds.getY(),
               (float)bounds.getX(), (float)bounds.getBottom(), 1.0f);
    g.drawLine((float)bounds.getRight(), (float)bounds.getY(),
               (float)bounds.getRight(), (float)bounds.getBottom(), 1.0f);

    // Header
    headerBounds = juce::Rectangle<int>(bounds.getX(), bounds.getY(),
                                         bounds.getWidth(), Theme::headerHeight);
    constexpr float disabledDarken = 0.35f;
    auto headerCol = Theme::color(Theme::Color::bgHeaderOut);
    if (!audioEnabled)
        headerCol = headerCol.interpolatedWith(juce::Colours::black, 1.0f - disabledDarken);
    g.setColour(headerCol);
    g.fillRect(headerBounds);

    // Power icon
    powerIconBounds = juce::Rectangle<int>(headerBounds.getX() + 6,
                                            headerBounds.getCentreY() - 7, 14, 14);
    {
        auto iconColor = audioEnabled ? Theme::color(Theme::Color::textWhite)
                                       : Theme::color(Theme::Color::textDim);
        g.setColour(iconColor);
        juce::Path powerIcon;
        auto iconArea = powerIconBounds.reduced(1).toFloat();
        powerIcon.addCentredArc(iconArea.getCentreX(), iconArea.getCentreY(),
                                 iconArea.getWidth() * 0.4f, iconArea.getHeight() * 0.4f,
                                 0.0f, juce::MathConstants<float>::pi * 0.3f,
                                 juce::MathConstants<float>::pi * 1.7f, true);
        g.strokePath(powerIcon, juce::PathStrokeType(1.5f));
        g.drawLine(iconArea.getCentreX(), iconArea.getY() + 1.0f,
                   iconArea.getCentreX(), iconArea.getCentreY(), 1.5f);
    }

    g.setColour(Theme::color(Theme::Color::textWhite));
    g.setFont(Theme::font(Theme::fontSize));
    g.drawText("Main", headerBounds.withTrimmedLeft(26).reduced(4, 0),
               juce::Justification::centredLeft);
}

void OutputStrip::setAudioEnabled(bool enabled) {
    if (audioEnabled != enabled) {
        audioEnabled = enabled;
        repaint();
    }
}

void OutputStrip::mouseUp(const juce::MouseEvent& event) {
    if (!event.mods.isPopupMenu() && powerIconBounds.expanded(6).contains(event.getPosition())) {
        audioEnabled = !audioEnabled;
        state.setMasterAudioEnabled(audioEnabled);
        repaint();
    }
}

void OutputStrip::resized() {
    auto bounds = getLocalBounds();
    constexpr int faderMeterWidth = 48;

    auto fmArea = bounds.withTrimmedTop(Theme::headerHeight + Theme::trackPadding)
                        .withTrimmedBottom(Theme::trackPadding)
                        .removeFromRight(faderMeterWidth + Theme::trackPadding)
                        .withTrimmedRight(Theme::trackPadding);
    faderMeter.setBounds(fmArea);

    auto slotArea = bounds.withTrimmedTop(Theme::headerHeight + Theme::trackPadding)
                          .withTrimmedLeft(Theme::trackPadding)
                          .withTrimmedRight(faderMeterWidth + Theme::trackPadding * 2);
    int y = slotArea.getY();

    for (auto& slot : effectSlots) {
        slot->setBounds(slotArea.getX(), y, slotArea.getWidth(), Theme::slotHeight);
        y += Theme::slotHeight + Theme::slotGap;
    }
}
