#include "gui/MainLayout.h"
#include "api/PerformanceAPI.h"

MainLayout::MainLayout(PerformanceAPI& api) : api(api), mixerView(api) {
    addChildComponent(sidebar);
    addAndMakeVisible(panel3);
    addAndMakeVisible(mixerView);
    setWantsKeyboardFocus(true);
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
    panel3.setBounds(area);
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
    auto c = key.getTextCharacter();

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
