#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/RegistryTree.h"

class Registry;

class Sidebar : public juce::Component, private juce::Timer {
public:
    Sidebar();

    void setRegistry(Registry* reg) { registry = reg; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshTree();

    Registry* registry = nullptr;
    RegistryTree tree;
};
