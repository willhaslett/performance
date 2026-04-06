#include "gui/MainLayout.h"
#include "api/PerformanceAPI.h"

MainLayout::MainLayout(PerformanceAPI& api) : api(api), mixerView(api) {
    sidebar.setAPI(&api);
    sidebar.setRegistry(&api.getRegistry());
    addAndMakeVisible(sidebar);
    addAndMakeVisible(terminalView);
    addAndMakeVisible(mixerView);
    setWantsKeyboardFocus(true);

    // Launch Claude Code in the runtime directory (has its own CLAUDE.md)
    auto workDir = juce::File(juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                       .getFullPathName());
    for (int i = 0; i < 5; ++i)
        workDir = workDir.getParentDirectory();

    auto runtimeDir = workDir.getChildFile("runtime");
    if (!runtimeDir.getChildFile("CLAUDE.md").existsAsFile())
        runtimeDir = juce::File("/Users/will/ideas_and_projects/performance/runtime");

    // Add project bin to PATH so claude can find the perf command
    auto binDir = runtimeDir.getParentDirectory().getChildFile("bin").getFullPathName();
    auto path = juce::String(getenv("PATH")) + ":" + binDir;
    setenv("PATH", path.toRawUTF8(), 1);

    terminalView.start("claude", runtimeDir.getFullPathName());
}

void MainLayout::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff121212));

    // Toolbar background
    auto toolbar = getLocalBounds().removeFromTop(toolbarHeight);
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRect(toolbar);
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawLine(0.0f, (float)toolbarHeight, (float)getWidth(), (float)toolbarHeight, 1.0f);

    // Sidebar toggle arrow in toolbar
    auto toggleBounds = juce::Rectangle<int>(4, 4, 24, 24);
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRoundedRectangle(toggleBounds.toFloat(), 4.0f);

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
    g.setColour(juce::Colour(0xff888888));
    g.fillPath(arrow);

    // Mode indicator
    if (insertMode) {
        g.setColour(juce::Colour(0xff44aa44));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("-- INSERT --", toolbar.withTrimmedLeft(36), juce::Justification::centredLeft);
    }
}

void MainLayout::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(toolbarHeight);

    if (sidebarOpen) {
        sidebar.setBounds(area.removeFromLeft(sidebarWidth));
    }

    if (mixerVisible) {
        int mixerHeight = (int)(area.getHeight() * mixerRatio);
        mixerView.setBounds(area.removeFromBottom(mixerHeight));
    }
    terminalView.setBounds(area);
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

bool MainLayout::terminalHasFocus() const {
    return insertMode;
}

bool MainLayout::handleGlobalKey(const juce::KeyPress& key) {
    // INSERT mode: all keys go to terminal, Escape returns to normal
    if (insertMode) {
        if (key == juce::KeyPress::escapeKey) {
            insertMode = false;
            repaint();  // update mode indicator
            return true;
        }
        terminalView.keyPressed(key);
        return true;
    }

    // NORMAL mode: app shortcuts
    auto c = key.getTextCharacter();

    // i — enter insert mode (terminal)
    if (c == 'i') {
        insertMode = true;
        repaint();
        return true;
    }

    if (c == 's') {
        sidebarOpen = !sidebarOpen;
        sidebar.setVisible(sidebarOpen);
        resized();
        repaint();
        return true;
    }

    // x — toggle mixer
    if (c == 'x') {
        mixerVisible = !mixerVisible;
        mixerView.setVisible(mixerVisible);
        resized();
        return true;
    }

    if (key == juce::KeyPress::escapeKey) {
        api.closeTopPluginEditor();
        return true;
    }

    return false;
}
