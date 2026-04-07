#include "gui/BusStrip.h"
#include "api/PerformanceAPI.h"

BusStrip::BusStrip(const juce::String& name, PerformanceAPI& api)
    : api(api), busName(name) {

    addAndMakeVisible(faderMeter);

    faderMeter.onGainChanged = [&](float newGain) {
        api.setBusGain(busName, newGain);
    };

    // Start with one empty effect slot
    rebuildEffectSlots();
}

void BusStrip::setEffects(const std::vector<PerformanceAPI::EffectSlotInfo>& effects) {
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
        currentEffects = effects;
        rebuildEffectSlots();
    }
}

void BusStrip::setPeakLevel(float level) {
    faderMeter.setPeakLevel(level);
}

void BusStrip::setGain(float gain) {
    faderMeter.setGain(gain);
}

void BusStrip::rebuildEffectSlots() {
    for (auto& slot : effectSlots)
        removeChildComponent(slot.get());
    effectSlots.clear();

    for (auto& fx : currentEffects) {
        auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, api, busName, PluginSlot::OnBus);
        slot->setPluginName(fx.pluginName);
        slot->setEffectId(fx.effectId);
        addAndMakeVisible(*slot);
        effectSlots.push_back(std::move(slot));
    }

    // Always an empty slot for adding new effect
    auto slot = std::make_unique<PluginSlot>(PluginSlot::Effect, api, busName, PluginSlot::OnBus);
    addAndMakeVisible(*slot);
    effectSlots.push_back(std::move(slot));

    resized();
}

void BusStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    g.setColour(Theme::color(Theme::Color::bgTrack));
    g.fillRect(bounds);

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)bounds.getRight(), (float)bounds.getY(),
               (float)bounds.getRight(), (float)bounds.getBottom(), 1.0f);

    // Header — purple tint for busses
    headerBounds = juce::Rectangle<int>(bounds.getX(), bounds.getY(),
                                         bounds.getWidth(), Theme::headerHeight);
    g.setColour(Theme::color(Theme::Color::bgHeaderBus));
    g.fillRect(headerBounds);

    g.setColour(Theme::color(Theme::Color::textWhite));
    g.setFont(Theme::font(Theme::fontSize));
    g.drawText(busName, headerBounds.reduced(8, 0), juce::Justification::centredLeft);
}

int BusStrip::getMinimumHeight() const {
    int h = Theme::headerHeight + Theme::trackPadding;
    h += (int)effectSlots.size() * (Theme::slotHeight + Theme::slotGap);
    h += Theme::trackPadding;
    return h;
}

void BusStrip::mouseUp(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu() && headerBounds.contains(event.getPosition())) {
        juce::PopupMenu menu;
        menu.addItem(1, "Delete Bus");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
            [this](int result) {
                if (result == 1)
                    api.removeBus(busName);
            });
    }
}

void BusStrip::mouseDoubleClick(const juce::MouseEvent& event) {
    if (headerBounds.contains(event.getPosition())) {
        nameEditor.onCommit = [this](const juce::String& newName) {
            if (newName != busName) {
                api.renameBus(busName, newName);
                busName = newName;
                repaint();
            }
        };
        nameEditor.show(*this, headerBounds.reduced(8, 4), busName);
    }
}

void BusStrip::resized() {
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
