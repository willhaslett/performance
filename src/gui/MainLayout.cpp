#include "gui/MainLayout.h"
#include "gui/KeyBindings.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"

MainLayout::MainLayout(StateAPI& state, EngineAPI& engine, LuaEngine& lua)
    : state(state), engine(engine), chatView(lua), mixerView(state, engine) {
    sidebar.setStateAPI(&state);
    addAndMakeVisible(sidebar);
    // Pane container: placeholder left (60%), chat right (40%)
    paneContainer.addPane(&placeholderPane, 0.6f);
    paneContainer.addPane(&chatView, 0.4f);
    addAndMakeVisible(paneContainer);
    addAndMakeVisible(mixerView);

    // Sidebar divider (vertical)
    addAndMakeVisible(sidebarDivider);
    sidebarDivider.onDragStart = [this]() { dragStartSidebarWidth = sidebarWidth; };
    sidebarDivider.onDrag = [this](int delta) {
        sidebarWidth = std::max(minPaneSize, dragStartSidebarWidth + delta);
        resized();
    };

    setWantsKeyboardFocus(true);

    // Load system prompt for Claude
    auto workDir = juce::File(juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                       .getFullPathName());
    for (int i = 0; i < 5; ++i)
        workDir = workDir.getParentDirectory();

    auto runtimeDir = workDir.getChildFile("runtime");
    if (!runtimeDir.getChildFile("CLAUDE.md").existsAsFile())
        runtimeDir = juce::File("/Users/will/ideas_and_projects/performance/runtime");

    auto claudeMd = runtimeDir.getChildFile("CLAUDE.md");
    if (claudeMd.existsAsFile()) {
        auto preamble = juce::String(
            "You have a `perf` tool that executes Lua code directly in the running performance engine. "
            "Use tool calls instead of shell commands. The `code` parameter takes the same Lua that the "
            "API docs below describe. Always use the perf tool to make changes — never suggest shell commands.\n\n");
        chatView.setSystemPrompt(preamble + claudeMd.loadFileAsString());
    }
}

void MainLayout::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    // Toolbar
    auto toolbar = getLocalBounds().removeFromTop(toolbarHeight);
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(toolbar);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)toolbarHeight, (float)getWidth(), (float)toolbarHeight, 1.0f);

    // Sidebar toggle
    auto toggleBounds = juce::Rectangle<int>(4, 4, 24, 24);
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(toggleBounds.toFloat(), Theme::cornerRadiusSm);

    juce::Path arrow;
    auto a = toggleBounds.reduced(7).toFloat();
    if (sidebarOpen) {
        arrow.addTriangle(a.getRight(), a.getY(),
                          a.getRight(), a.getBottom(),
                          a.getX(), a.getCentreY());
    } else {
        arrow.addTriangle(a.getX(), a.getY(),
                          a.getX(), a.getBottom(),
                          a.getRight(), a.getCentreY());
    }
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.fillPath(arrow);

}

void MainLayout::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(toolbarHeight);

    // Sidebar + divider
    if (sidebarOpen) {
        sidebar.setBounds(area.removeFromLeft(sidebarWidth));
        sidebarDivider.setBounds(sidebarWidth, area.getY(),
                                  Divider::thickness, area.getHeight());
        sidebarDivider.setVisible(true);
        sidebarDivider.toFront(false);
    } else {
        sidebarDivider.setVisible(false);
    }

    // Mixer — height driven by content
    if (mixerVisible) {
        int contentHeight = mixerView.getDesiredHeight();
        auto mh = std::max(minPaneSize, std::min(contentHeight, area.getHeight() - minPaneSize));
        mixerView.setBounds(area.removeFromBottom(mh));
    }

    // Pane container fills remaining
    paneContainer.setBounds(area);
}

void MainLayout::mouseUp(const juce::MouseEvent& event) {
    auto toggleBounds = juce::Rectangle<int>(4, 4, 24, 24);
    if (toggleBounds.contains(event.getPosition())) {
        sidebarOpen = !sidebarOpen;
        sidebar.setVisible(sidebarOpen);
        resized();
        repaint();
    }
}

bool MainLayout::handleGlobalKey(const juce::KeyPress& key) {
    if (key == KeyBindings::toggleSidebar) {
        sidebarOpen = !sidebarOpen;
        sidebar.setVisible(sidebarOpen);
        resized();
        repaint();
        return true;
    }

    if (key == KeyBindings::toggleMixer) {
        mixerVisible = !mixerVisible;
        mixerView.setVisible(mixerVisible);
        resized();
        return true;
    }

    if (key == KeyBindings::save) {
        if (onSave) onSave();
        return true;
    }

    if (key == KeyBindings::closeEditor) {
        engine.closeTopPluginEditor();
        return true;
    }

    return false;
}
