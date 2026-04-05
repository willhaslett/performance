#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/RegistryTree.h"

class PerformanceAPI;
class Registry;

class Sidebar : public juce::Component {
public:
    Sidebar();
    ~Sidebar() override;

    void setAPI(PerformanceAPI* a) { api = a; }
    void setRegistry(Registry* reg);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void refreshTree();

    PerformanceAPI* api = nullptr;
    Registry* registry = nullptr;
    RegistryTree tree;
    int subscriptionId = -1;
    bool needsRefresh = false;
};
