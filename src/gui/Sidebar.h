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
        enum Kind { SectionHeader, ViewToggle, SongEntry, ActionButton };
        Kind kind;
        juce::String label;
        std::string id;        // view name or song id
        juce::Rectangle<int> bounds;
    };
    std::vector<Item> items;
    int hoveredIndex = -1;

    void rebuild();
    void timerCallback() override;

    static constexpr int sectionHeight = 28;
    static constexpr int itemHeight = 26;
    static constexpr int sectionGap = 12;
    static constexpr int leftPad = 12;
};
