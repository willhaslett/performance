#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/PluginSlot.h"
#include "gui/FaderMeter.h"
#include "gui/SendsPanel.h"
#include "gui/InlineEditor.h"
#include "gui/SaveAsDialog.h"
#include "api/PerformanceAPI.h"
#include <vector>
#include <memory>

class TrackStrip : public juce::Component {
public:
    TrackStrip(const juce::String& id, const juce::String& name, PerformanceAPI& api);

    void setInstrumentName(const juce::String& name);
    void setEffects(const std::vector<PerformanceAPI::EffectSlotInfo>& effects);
    void setMidiEnabled(bool enabled);
    void setPeakLevel(float level);
    void setGain(float gain);
    void setSends(const std::vector<SendsPanel::SendInfo>& sends);
    void setAvailableBusses(const std::vector<SendsPanel::BusOption>& busOptions);

    int getMinimumHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    PerformanceAPI& api;
    juce::String trackId;    // stable UUID from registry
    juce::String trackName;  // display name
    bool midiEnabled = true;

    PluginSlot instrumentSlot;
    std::vector<std::unique_ptr<PluginSlot>> effectSlots;
    FaderMeter faderMeter;
    SendsPanel sendsPanel;
    InlineEditor nameEditor;

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> midiDotBounds;
    juce::Rectangle<int> menuDotsBounds;

    void showTrackMenu(juce::Point<int> screenPos);

    void rebuildEffectSlots();
    std::vector<PerformanceAPI::EffectSlotInfo> currentEffects;
};
