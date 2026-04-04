#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class Sidebar : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1a1a1a));

        // Right border
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawLine((float)getWidth(), 0.0f, (float)getWidth(), (float)getHeight(), 1.0f);
    }
};
