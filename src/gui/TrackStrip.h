#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/PluginSlot.h"
#include "gui/FaderMeter.h"
#include "gui/SendsPanel.h"
#include <vector>
#include <memory>

class PerformanceAPI;

class TrackStrip : public juce::Component {
public:
    TrackStrip(const juce::String& name, PerformanceAPI& api);

    void setInstrumentName(const juce::String& name);
    void setEffectNames(const std::vector<juce::String>& names);
    void setMidiEnabled(bool enabled);
    void setPeakLevel(float level);
    void setGain(float gain);
    void setSends(const std::vector<SendsPanel::SendInfo>& sends);
    void setAvailableBusses(const std::vector<juce::String>& busNames);

    int getMinimumHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    PerformanceAPI& api;
    juce::String trackName;
    bool midiEnabled = true;

    PluginSlot instrumentSlot;
    std::vector<std::unique_ptr<PluginSlot>> effectSlots;
    FaderMeter faderMeter;
    SendsPanel sendsPanel;

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> midiDotBounds;

    void rebuildEffectSlots();
    std::vector<juce::String> currentEffectNames;
};
