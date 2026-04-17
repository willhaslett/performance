#include "gui/PerformPane.h"
#include "gui/Theme.h"

PerformPane::PerformPane(StateAPI& state, EngineAPI& engine,
                         PerformanceCoordinator& coord)
    : controllersPane(state, engine, coord),
      songMappingsPane(state, engine, coord) {
    addAndMakeVisible(controllersPane);
    addAndMakeVisible(songMappingsPane);
    addAndMakeVisible(divider);

    // Wire the Controllers→SongMappings refresh trigger that MainLayout used to own.
    controllersPane.onBindingsChanged = [this] { songMappingsPane.refresh(); };

    divider.onDragStart = [this] {
        dragStartControllersWidth = (int)(getWidth() * controllersRatio);
    };
    divider.onDrag = [this](int delta) {
        if (getWidth() <= 0) return;
        int w = juce::jlimit(minSide, getWidth() - minSide,
                             dragStartControllersWidth + delta);
        controllersRatio = (float)w / (float)getWidth();
        resized();
    };
}

void PerformPane::paintOverChildren(juce::Graphics& g) {
    // Right-edge border — drawn after children so SongMappings doesn't overpaint it.
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)getWidth() - 1, 0.0f, (float)getWidth() - 1, (float)getHeight(), 1.0f);
}

void PerformPane::resized() {
    auto area = getLocalBounds();
    if (area.getWidth() < minSide * 2 + Divider::thickness) {
        // Too narrow to split — give everything to Controllers.
        controllersPane.setBounds(area);
        songMappingsPane.setBounds({});
        divider.setBounds({});
        return;
    }
    int leftW = juce::jlimit(minSide, area.getWidth() - minSide - Divider::thickness,
                              (int)(area.getWidth() * controllersRatio));
    controllersPane.setBounds(area.removeFromLeft(leftW));
    divider.setBounds(area.removeFromLeft(Divider::thickness));
    songMappingsPane.setBounds(area);
}
