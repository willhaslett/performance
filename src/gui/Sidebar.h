#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/RegistryTree.h"
#include "gui/Theme.h"

class StateAPI;

class Sidebar : public juce::Component, private juce::Timer {
public:
    Sidebar();
    ~Sidebar() override;

    void setStateAPI(StateAPI* s);

    // Callback for song loading (coordinator-level operation)
    std::function<void(const std::string& songId)> onLoadSong;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshTree();

    StateAPI* state = nullptr;
    RegistryTree tree;
    int subscriptionId = -1;
    bool needsRefresh = false;
    std::string lastHighlightedId;
};
