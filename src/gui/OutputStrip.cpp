#include "gui/OutputStrip.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"

OutputStrip::OutputStrip(StateAPI& state, EngineAPI& engine)
    : state(state), engine(engine) {
    addAndMakeVisible(faderMeter);

    faderMeter.onGainChanged = [&](float newGain) {
        state.setMasterGain(newGain);
    };
    faderMeter.onDragStart = [&]() { state.beginTransaction(); };
    faderMeter.onDragEnd = [&]() { state.endTransaction(); };
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

    g.setColour(Theme::color(Theme::Color::bgSurface));
    g.fillRect(bounds);

    // Header
    headerBounds = juce::Rectangle<int>(bounds.getX(), bounds.getY(),
                                         bounds.getWidth(), Theme::headerHeight);
    g.setColour(Theme::color(Theme::Color::bgSurface));
    g.fillRect(headerBounds);

    g.setColour(Theme::color(Theme::Color::textOnColor));
    g.setFont(Theme::font(Theme::fontSizeLg));
    g.drawText("Main", headerBounds.reduced(Theme::spacingM, 0),
               juce::Justification::centredLeft);

    // Edge borders — drawn last
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)bounds.getX(), (float)bounds.getY(),
               (float)bounds.getX(), (float)bounds.getBottom(), 1.0f);
    g.drawLine((float)bounds.getRight(), (float)bounds.getY(),
               (float)bounds.getRight(), (float)bounds.getBottom(), 1.0f);
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
