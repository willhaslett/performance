#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include <vector>
#include <functional>

class PerformanceAPI;

class TrackStrip : public juce::Component {
public:
    TrackStrip(const juce::String& name, PerformanceAPI& api);

    void setInstrumentName(const juce::String& name);
    void setEffectNames(const std::vector<juce::String>& names);
    void setMidiEnabled(bool enabled);

    void paint(juce::Graphics& g) override;
    void resized() override {}
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    PerformanceAPI& api;
    juce::String trackName;
    juce::String instrumentName;       // empty = no instrument loaded
    std::vector<juce::String> effectNames;
    bool midiEnabled = true;
    int hoveredSlot = -1;              // -1=none, 0=instrument, 1+=effects

    struct SlotBounds {
        juce::Rectangle<int> bounds;
        int index;  // 0=instrument, 1+=effect index
    };
    std::vector<SlotBounds> slots;

    void rebuildSlots();
    void showSlotContextMenu(int slotIndex, bool isInstrument, juce::Point<int> position);
    void showPluginPicker(int slotIndex, juce::Point<int> position);
    juce::Rectangle<int> headerBounds;
};
