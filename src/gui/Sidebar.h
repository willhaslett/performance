#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class Sidebar : public juce::Component {
public:
    std::function<void()> onToggle;

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1a1a1a));

        // Right border
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawLine((float)getWidth(), 0.0f, (float)getWidth(), (float)getHeight(), 1.0f);

        // Close arrow — top right
        auto toggleBounds = juce::Rectangle<int>(getWidth() - 28, 4, 24, 24);
        g.setColour(juce::Colour(0xff2a2a2a));
        g.fillRoundedRectangle(toggleBounds.toFloat(), 4.0f);

        juce::Path arrow;
        auto a = toggleBounds.reduced(7).toFloat();
        arrow.addTriangle(a.getRight(), a.getY(),
                          a.getRight(), a.getBottom(),
                          a.getX(), a.getCentreY());
        g.setColour(juce::Colour(0xff888888));
        g.fillPath(arrow);
    }

    void mouseUp(const juce::MouseEvent& event) override {
        auto toggleBounds = juce::Rectangle<int>(getWidth() - 28, 4, 24, 24);
        if (toggleBounds.contains(event.getPosition()) && onToggle)
            onToggle();
    }
};
