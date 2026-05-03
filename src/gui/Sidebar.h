#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include <string>
#include <vector>
#include <functional>

class StateAPI;
class EngineAPI;
class PerformanceCoordinator;

// Sidebar — categorized list of navigation items and content.
// Always expanded, one level deep: section headers with clickable items below.
// Replaces the old tabbed tree view.

class Sidebar : public juce::Component, private juce::Timer {
public:
    Sidebar();
    ~Sidebar() override;

    void setStateAPI(StateAPI* s);
    void setEngineAPI(EngineAPI* e);
    void setCoordinator(PerformanceCoordinator* c);

    // Callbacks
    std::function<void(const std::string& songId)> onLoadSong;
    std::function<void(const std::string& songId)> onDeleteSong;
    std::function<void(const std::string& deviceName)> onAudioOutputSelected;
    std::function<void(const std::string& deviceName)> onAudioInputSelected;

    // Track presets — list provider + click handler. Click creates a
    // new track named after the preset, then loads the preset onto it
    // (mirrors the right-click "Load Track Preset" submenu but
    // entry-point is "I want a fresh track from a preset" rather than
    // "swap an existing track's chain").
    std::function<std::vector<juce::String>()> onListTrackPresets;
    std::function<void(const juce::String& presetName)> onTrackPresetClicked;

    // Navigation callbacks — MainLayout sets these
    std::function<void(const std::string& viewName)> onToggleView;
    std::function<bool(const std::string& viewName)> isViewActive;
    std::function<void()> onNewSong;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    StateAPI* state = nullptr;
    EngineAPI* engineAPI = nullptr;
    PerformanceCoordinator* coordinator = nullptr;
    int subscriptionId = -1;

    // Item model — rebuilt on refresh
    struct Item {
        enum Kind { SectionHeader, ViewToggle, SongEntry, TrackPresetEntry, ActionButton };
        Kind kind;
        juce::String label;
        std::string id;        // view name, song id, or preset name
        juce::Rectangle<int> bounds;
        juce::String shortcut; // keybinding hint (e.g., "⌘P")
    };
    std::vector<Item> items;
    int hoveredIndex = -1;

    void rebuild();
    void timerCallback() override;

    static constexpr int sectionHeight = 32;
    static constexpr int itemHeight = 30;
    static constexpr int sectionGap = 16;
    static constexpr int leftPad = 14;
    static constexpr int labelWidth = 80;   // fixed-width label column — keybinding hints left-align against its right edge
};
