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
        g.setColour(Theme::color(Theme::Color::border));
        float mid = orientation == Horizontal
            ? (float)getHeight() * 0.5f
            : (float)getWidth() * 0.5f;
        if (orientation == Horizontal)
            g.drawLine(0.0f, mid, (float)getWidth(), mid, 1.0f);
        else
            g.drawLine(mid, 0.0f, mid, (float)getHeight(), 1.0f);
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (onDragStart) onDragStart();
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        int delta = orientation == Horizontal ? event.getDistanceFromDragStartY()
                                               : event.getDistanceFromDragStartX();
        if (onDrag) onDrag(delta);
    }

    static constexpr int thickness = 3;

private:
    Orientation orientation;
};
