#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Sidebar.h"
#include "gui/MixerView.h"
#include "gui/ChatView.h"
#include "gui/PaneContainer.h"
#include "gui/Divider.h"

class StateAPI;
class EngineAPI;
class LuaEngine;

class MainLayout : public juce::Component {
public:
    MainLayout(StateAPI& state, EngineAPI& engine, LuaEngine& lua);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

    bool handleGlobalKey(const juce::KeyPress& key);

    Sidebar& getSidebar() { return sidebar; }
    MixerView& getMixer() { return mixerView; }

private:
    StateAPI& state;
    EngineAPI& engine;

    Sidebar sidebar;
    ChatView chatView;
    // Left pane placeholder for future views (Mapping, etc.)
    class PlaceholderPane : public juce::Component {
    public:
        void paint(juce::Graphics& g) override {
            g.fillAll(Theme::color(Theme::Color::bgApp));
            g.setColour(Theme::color(Theme::Color::textDim));
            g.setFont(Theme::font(Theme::fontSize));
            g.drawText("Mapping", getLocalBounds(), juce::Justification::centred);
        }
    };
    PlaceholderPane placeholderPane;
    PaneContainer paneContainer;
    MixerView mixerView;

    Divider sidebarDivider { Divider::Vertical };

    bool sidebarOpen = true;
    bool mixerVisible = true;

    int sidebarWidth = 240;
    int dragStartSidebarWidth = 0;
    static constexpr int toolbarHeight = 32;
    static constexpr int minPaneSize = 100;
};
