#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/PluginSlot.h"
#include "gui/FaderMeter.h"
#include "gui/InlineEditor.h"
#include "state/Id.h"
#include <vector>
#include <memory>

class StateAPI;
class EngineAPI;

class BusStrip : public juce::Component {
public:
    BusStrip(const juce::String& id, const juce::String& name,
             StateAPI& state, EngineAPI& engine);

    struct EffectSlotInfo { juce::String effectId; juce::String pluginName; };

    void setEffects(const std::vector<EffectSlotInfo>& effects);
    void setPeakLevel(float level);
    void setPeakLevelStereo(float left, float right);
    void setGain(float gain);

    int getMinimumHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void setOutputTarget(const juce::String& target, const juce::String& displayName);

    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    // Bus preset callbacks (set by MixerView, implemented by coordinator)
    std::function<void(const juce::String& busId, const juce::String& presetName)> onSaveBusPreset;
    std::function<void(const juce::String& busId, const juce::String& presetName)> onLoadBusPreset;
    std::function<std::vector<juce::String>()> onListBusPresets;

private:
    // Remove a bus with a confirmation dialog when it has dependent action
    // events / bindings. Cascades their removal on confirm.
    void confirmAndRemoveBus(const BusId& id);

    StateAPI& state;
    EngineAPI& engine;
    juce::String busId;    // stable UUID from registry
    juce::String busName;  // display name

    std::vector<std::unique_ptr<PluginSlot>> effectSlots;
    FaderMeter faderMeter;

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> outputTargetBounds;
    juce::String outputTargetId;
    juce::String outputTargetDisplay;
    InlineEditor nameEditor;

    void rebuildEffectSlots();
    std::vector<EffectSlotInfo> currentEffects;
    bool pendingEffectOpen = false;
};
