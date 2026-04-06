#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include <functional>

// Draggable divider between panes
class Divider : public juce::Component {
public:
    enum Orientation { Horizontal, Vertical };

    Divider(Orientation orient) : orientation(orient) {
        setMouseCursor(orient == Horizontal
            ? juce::MouseCursor::UpDownResizeCursor
            : juce::MouseCursor::LeftRightResizeCursor);
        setOpaque(false);
    }

    std::function<void()> onDragStart;
    std::function<void(int delta)> onDrag;

    void paint(juce::Graphics& g) override {
        // Transparent background, just draw the divider line at the edge
        g.setColour(Theme::color(Theme::Color::border));
        if (orientation == Horizontal)
            g.drawLine(0.0f, 0.0f, (float)getWidth(), 0.0f, 1.0f);
        else
            g.drawLine(0.0f, 0.0f, 0.0f, (float)getHeight(), 1.0f);
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (onDragStart) onDragStart();
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        int delta = orientation == Horizontal ? event.getDistanceFromDragStartY()
                                               : event.getDistanceFromDragStartX();
        if (onDrag) onDrag(delta);
    }

    static constexpr int thickness = 6;

private:
    Orientation orientation;
};
