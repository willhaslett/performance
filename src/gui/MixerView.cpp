#include "gui/MixerView.h"
#include "api/PerformanceAPI.h"
#include "engine/Log.h"

MixerView::MixerView(PerformanceAPI& api) : api(api) {
    addChildComponent(sidebar);
    sidebar.onToggle = [this] {
        sidebarOpen = false;
        sidebar.setVisible(false);
        resized();
        repaint();
    };
    startTimerHz(4);
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff121212));

    // Draw open-sidebar arrow (only when closed — sidebar draws its own close arrow)
    if (!sidebarOpen) {
        auto toggleBounds = juce::Rectangle<int>(4, 4, 24, 24);
        g.setColour(juce::Colour(0xff2a2a2a));
        g.fillRoundedRectangle(toggleBounds.toFloat(), 4.0f);

        juce::Path arrow;
        auto a = toggleBounds.reduced(7).toFloat();
        arrow.addTriangle(a.getX(), a.getY(),
                          a.getX(), a.getBottom(),
                          a.getRight(), a.getCentreY());
        g.setColour(juce::Colour(0xff888888));
        g.fillPath(arrow);
    }

    // "No tracks" message
    if (strips.empty()) {
        auto area = getLocalBounds();
        if (sidebarOpen) area.removeFromLeft(sidebarWidth);
        g.setColour(juce::Colour(0xff666666));
        g.setFont(16.0f);
        g.drawText("No tracks loaded", area, juce::Justification::centred);
    }
}

void MixerView::resized() {
    auto area = getLocalBounds();

    if (sidebarOpen) {
        sidebar.setBounds(area.removeFromLeft(sidebarWidth));
    }

    // Track strips
    auto mixerArea = area.reduced(8).withTrimmedTop(32);
    if (strips.empty()) return;

    int stripWidth = std::min(180, mixerArea.getWidth() / (int)strips.size());
    for (auto& strip : strips) {
        strip->setBounds(mixerArea.removeFromLeft(stripWidth).reduced(2));
    }
}

void MixerView::mouseUp(const juce::MouseEvent& event) {
    if (!sidebarOpen) {
        auto toggleBounds = juce::Rectangle<int>(4, 4, 24, 24);
        if (toggleBounds.contains(event.getPosition())) {
            sidebarOpen = true;
            sidebar.setVisible(true);
            resized();
            repaint();
        }
    }
}

void MixerView::timerCallback() {
    auto trackNames = api.listTrackNames();
    if (trackNames != lastTrackNames) {
        lastTrackNames = trackNames;
        rebuildStrips();
    }
}

void MixerView::rebuildStrips() {
    strips.clear();

    for (auto& trackName : lastTrackNames) {
        auto strip = std::make_unique<TrackStrip>(trackName,
            [this](const juce::String& track, const juce::String& plugin, bool isInstrument) {
                if (isInstrument)
                    api.openPluginEditor(track);
                else
                    api.openPluginEditor(track, plugin);
            });

        std::vector<TrackStrip::PluginEntry> plugins;

        auto instrumentName = api.getTrackPluginName(trackName);
        if (instrumentName.isNotEmpty())
            plugins.push_back({ instrumentName, true });

        for (auto& fx : api.getTrackEffectNames(trackName))
            plugins.push_back({ fx, false });

        strip->setPlugins(plugins);
        strip->setMidiEnabled(api.isTrackMidiEnabled(trackName));

        addAndMakeVisible(*strip);
        strips.push_back(std::move(strip));
    }

    resized();
}
