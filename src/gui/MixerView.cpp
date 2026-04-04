#include "gui/MixerView.h"
#include "api/PerformanceAPI.h"

MixerView::MixerView(PerformanceAPI& api) : api(api) {
    startTimerHz(4);
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff121212));

    if (strips.empty()) {
        g.setColour(juce::Colour(0xff666666));
        g.setFont(16.0f);
        g.drawText("No tracks loaded", getLocalBounds(), juce::Justification::centred);
    }
}

void MixerView::resized() {
    if (strips.empty()) return;

    auto area = getLocalBounds().reduced(8);
    int stripWidth = std::min(180, area.getWidth() / (int)strips.size());

    for (auto& strip : strips) {
        strip->setBounds(area.removeFromLeft(stripWidth).reduced(2));
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
