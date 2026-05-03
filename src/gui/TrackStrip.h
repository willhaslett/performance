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
    void setArmed(bool armed);
    void setInputMonitoring(bool enabled);
    void setMuted(bool muted);
    void setSoloed(bool soloed);
    void setPeakLevel(float level);
    void setPeakLevelStereo(float left, float right);
    void setGain(float gain);
    void setSends(const std::vector<SendsPanel::SendInfo>& sends);
    void setAvailableBusses(const std::vector<SendsPanel::BusOption>& busOptions);
    void setOutputTarget(const juce::String& target, const juce::String& displayName);

    void setSourceType(TrackSourceType type);
    void setInputChannels(int start, int count, const std::vector<juce::String>& availableInputs);

    // Callbacks for coordinator-level operations (track presets)
    std::function<void(const juce::String& trackId, const juce::String& presetName)> onSaveTrackPreset;
    std::function<void(const juce::String& trackId, const juce::String& presetName)> onLoadTrackPreset;
    std::function<std::vector<juce::String>()> onListTrackPresets;

    // Drag reorder callbacks (set by MixerView)
    std::function<void(const juce::String& trackId, int x)> onDragStart;
    std::function<void(int mouseX)> onDragMove;
    std::function<void()> onDragEnd;

    int getMinimumHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    StateAPI& state;
    EngineAPI& engine;
    juce::String trackId;    // stable UUID from registry
    juce::String trackName;  // display name
    bool armed = false;
    bool inputMonitoring = true;
    bool muted = false;
    bool soloed = false;

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
    // Track removal moved to TrackUi::confirmAndRemoveTrack — shared
    // with the Producer's track-header right-click menu.

    PluginSlot instrumentSlot;
    std::vector<std::unique_ptr<PluginSlot>> effectSlots;
    FaderMeter faderMeter;
    SendsPanel sendsPanel;
    InlineEditor nameEditor;

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> midiDotBounds;
    juce::Rectangle<int> armDotBounds;
    juce::Rectangle<int> inputMonitorBounds;
    juce::Rectangle<int> muteBounds;
    juce::Rectangle<int> soloBounds;

    // Pill hover state (only applied to resting/off pills)
    enum class HoveredPill { None, Mute, Solo };
    HoveredPill hoveredPill = HoveredPill::None;
    juce::Rectangle<int> menuDotsBounds;
    juce::Rectangle<int> outputTargetBounds;
    juce::String outputTargetId;
    juce::String outputTargetDisplay;
    void showOutputTargetMenu(juce::Point<int> screenPos);

    void showTrackMenu(juce::Point<int> screenPos);

    void rebuildEffectSlots();
    std::vector<EffectSlotInfo> currentEffects;
    bool pendingEffectOpen = false;
    bool dragStarted = false;
};
