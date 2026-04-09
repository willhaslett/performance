#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "gui/PluginSlot.h"
#include "gui/FaderMeter.h"
#include "gui/SendsPanel.h"
#include "gui/InlineEditor.h"
#include "gui/SaveAsDialog.h"
#include "state/StateModel.h"
#include <vector>
#include <memory>

class StateAPI;
class EngineAPI;

class TrackStrip : public juce::Component {
public:
    TrackStrip(const juce::String& id, const juce::String& name,
               StateAPI& state, EngineAPI& engine);

    struct EffectSlotInfo { juce::String effectId; juce::String pluginName; };

    void setInstrumentName(const juce::String& name);
    void setEffects(const std::vector<EffectSlotInfo>& effects);
    void setMidiEnabled(bool enabled);
    void setAudioEnabled(bool enabled);
    void setPeakLevel(float level);
    void setPeakLevelStereo(float left, float right);
    void setGain(float gain);
    void setSends(const std::vector<SendsPanel::SendInfo>& sends);
    void setAvailableBusses(const std::vector<SendsPanel::BusOption>& busOptions);

    void setSourceType(TrackSourceType type);
    void setInputChannels(int start, int count, const std::vector<juce::String>& availableInputs);

    // Callbacks for coordinator-level operations (track presets)
    std::function<void(const juce::String& trackId, const juce::String& presetName)> onSaveTrackPreset;
    std::function<void(const juce::String& trackId, const juce::String& presetName)> onLoadTrackPreset;
    std::function<std::vector<juce::String>()> onListTrackPresets;

    int getMinimumHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    StateAPI& state;
    EngineAPI& engine;
    juce::String trackId;    // stable UUID from registry
    juce::String trackName;  // display name
    bool midiEnabled = true;
    bool audioEnabled = true;

    TrackSourceType sourceType = TrackSourceType::Instrument;

    // Input selector (audio input tracks) — custom painted slot, not ComboBox
    juce::Rectangle<int> inputSlotBounds;
    int inputChannelStart = -1;
    int inputChannelCount = 0;
    juce::String inputDisplayName;  // "Input 1", "Input 1-2 (Stereo)", etc.
    std::vector<juce::String> cachedInputNames;
    bool inputSlotHovered = false;
    void showInputPicker(juce::Point<int> screenPos);
    void paintInputSlot(juce::Graphics& g);

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
    std::vector<EffectSlotInfo> currentEffects;
    bool pendingEffectOpen = false;
};
