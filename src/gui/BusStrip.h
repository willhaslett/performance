#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/PluginSlot.h"
#include "gui/FaderMeter.h"
#include <vector>
#include <memory>

class PerformanceAPI;

class BusStrip : public juce::Component {
public:
    BusStrip(const juce::String& name, PerformanceAPI& api);

    void setEffectNames(const std::vector<juce::String>& names);
    void setPeakLevel(float level);
    void setGain(float gain);

    int getMinimumHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PerformanceAPI& api;
    juce::String busName;

    std::vector<std::unique_ptr<PluginSlot>> effectSlots;
    FaderMeter faderMeter;

    juce::Rectangle<int> headerBounds;

    void rebuildEffectSlots();
    std::vector<juce::String> currentEffectNames;
};
