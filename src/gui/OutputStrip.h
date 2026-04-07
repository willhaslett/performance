#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/PluginSlot.h"
#include "gui/FaderMeter.h"
#include "api/PerformanceAPI.h"
#include <vector>
#include <memory>

class OutputStrip : public juce::Component {
public:
    OutputStrip(PerformanceAPI& api);

    void setEffects(const std::vector<PerformanceAPI::EffectSlotInfo>& effects);
    void setPeakLevel(float level);
    void setGain(float gain);

    int getMinimumHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PerformanceAPI& api;

    std::vector<std::unique_ptr<PluginSlot>> effectSlots;
    FaderMeter faderMeter;

    juce::Rectangle<int> headerBounds;

    void rebuildEffectSlots();
    std::vector<PerformanceAPI::EffectSlotInfo> currentEffects;
    bool pendingEffectOpen = false;
};
