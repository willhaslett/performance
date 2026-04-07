#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include <functional>

class PerformanceAPI;

class PluginSlot : public juce::Component, private juce::Timer {
public:
    enum Type { Instrument, Effect };

    enum ParentKind { OnTrack, OnBus };
    PluginSlot(Type type, PerformanceAPI& api, const juce::String& parentId,
               ParentKind parent = OnTrack);

    void setPluginName(const juce::String& name);
    void setEffectId(const juce::String& id) { effectId = id; }
    juce::String getPluginName() const { return pluginName; }
    juce::String getEffectId() const { return effectId; }
    bool hasPlugin() const { return pluginName.isNotEmpty(); }

    // Called after a plugin is selected or removed
    std::function<void()> onChanged;

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    Type slotType;
    ParentKind parentKind;
    PerformanceAPI& api;
    juce::String parentId;  // track/bus/song UUID
    juce::String pluginName;
    juce::String effectId;  // internal key for effect lookup (empty for instruments)
    bool hovered = false;
    bool waitingForLoad = false;

    void timerCallback() override;
    void showPicker(juce::Point<int> position);
    void showContextMenu(juce::Point<int> position);
};
