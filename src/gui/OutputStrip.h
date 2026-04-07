#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/PluginSlot.h"
#include "gui/FaderMeter.h"
#include <vector>
#include <memory>

class StateAPI;
class EngineAPI;

class OutputStrip : public juce::Component {
public:
    OutputStrip(StateAPI& state, EngineAPI& engine);

    struct EffectSlotInfo { juce::String effectId; juce::String pluginName; };

    void setEffects(const std::vector<EffectSlotInfo>& effects);
    void setPeakLevel(float level);
    void setGain(float gain);

    int getMinimumHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    StateAPI& state;
    EngineAPI& engine;

    std::vector<std::unique_ptr<PluginSlot>> effectSlots;
    FaderMeter faderMeter;

    juce::Rectangle<int> headerBounds;

    void rebuildEffectSlots();
    std::vector<EffectSlotInfo> currentEffects;
    bool pendingEffectOpen = false;
};
