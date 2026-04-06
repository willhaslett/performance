#include "gui/MixerView.h"
#include "api/PerformanceAPI.h"

MixerView::MixerView(PerformanceAPI& api) : api(api) {
    startTimerHz(4);
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    if (strips.empty()) {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(14.0f));
        g.drawText("No tracks", getLocalBounds(), juce::Justification::centred);
    }
}

void MixerView::resized() {
    if (strips.empty()) return;

    auto area = getLocalBounds().reduced(4);
    int stripWidth = std::min(Theme::trackStripWidth,
                               area.getWidth() / std::max(1, (int)strips.size()));

    for (auto& strip : strips) {
        strip->setBounds(area.removeFromLeft(stripWidth).reduced(2));
    }
}

void MixerView::timerCallback() {
    auto trackNames = api.listTrackNames();
    if (trackNames != lastTrackNames) {
        lastTrackNames = trackNames;
        rebuildStrips();
    } else {
        // Update existing strips with current state
        for (size_t i = 0; i < strips.size() && i < lastTrackNames.size(); ++i) {
            auto& name = lastTrackNames[i];
            strips[i]->setInstrumentName(api.getTrackPluginName(name));
            strips[i]->setEffectNames(api.getTrackEffectNames(name));
            strips[i]->setMidiEnabled(api.isTrackMidiEnabled(name));
        }
    }
}

void MixerView::rebuildStrips() {
    strips.clear();

    for (auto& trackName : lastTrackNames) {
        auto strip = std::make_unique<TrackStrip>(trackName, api);

        strip->setInstrumentName(api.getTrackPluginName(trackName));
        strip->setEffectNames(api.getTrackEffectNames(trackName));
        strip->setMidiEnabled(api.isTrackMidiEnabled(trackName));

        addAndMakeVisible(*strip);
        strips.push_back(std::move(strip));
    }

    resized();
}
