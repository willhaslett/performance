#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/RegistryTree.h"
#include "gui/Theme.h"

class PerformanceAPI;
class Registry;

class Sidebar : public juce::Component, private juce::Timer {
public:
    Sidebar();
    ~Sidebar() override;

    void setAPI(PerformanceAPI* a) { api = a; }
    void setRegistry(Registry* reg);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshTree();

    PerformanceAPI* api = nullptr;
    Registry* registry = nullptr;
    RegistryTree tree;
    int subscriptionId = -1;
    bool needsRefresh = false;
    std::string lastHighlightedId;
};
