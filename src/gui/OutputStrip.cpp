#include "gui/OutputStrip.h"
#include "api/PerformanceAPI.h"

OutputStrip::OutputStrip(PerformanceAPI& api) : api(api) {
    addAndMakeVisible(faderMeter);

    faderMeter.onGainChanged = [&](float newGain) {
        api.setMasterGain(newGain);
    };

    rebuildEffectSlots();
}

void OutputStrip::setEffects(const std::vector<PerformanceAPI::EffectSlotInfo>& effects) {
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
            api.openPluginEditor(api.getMasterOutputId(), newFx.effectId);
        }
    }
}

void OutputStrip::setPeakLevel(float level) {
    faderMeter.setPeakLevel(level);
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

    auto masterOutputId = api.getMasterOutputId();
    for (auto& fx : currentEffects) {
        auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, api, masterOutputId, PluginSlot::OnBus);
        slot->setPluginName(fx.pluginName);
        slot->setEffectId(fx.effectId);
        addAndMakeVisible(*slot);
        effectSlots.push_back(std::move(slot));
    }

    // Always an empty slot for adding new effect
    auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, api, masterOutputId, PluginSlot::OnBus);
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
    g.setColour(Theme::color(Theme::Color::bgHeaderOut));
    g.fillRect(headerBounds);

    g.setColour(Theme::color(Theme::Color::textWhite));
    g.setFont(Theme::font(Theme::fontSize));
    g.drawText("Output", headerBounds.reduced(8, 0), juce::Justification::centredLeft);
}

void OutputStrip::resized() {
    auto bounds = getLocalBounds();
    constexpr int faderMeterWidth = 28;

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
