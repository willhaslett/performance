#include "gui/MixerView.h"
#include "gui/SendsPanel.h"
#include "api/PerformanceAPI.h"

MixerView::MixerView(PerformanceAPI& api) : api(api) {
    setInterceptsMouseClicks(true, true);
    setPaintingIsUnclipped(false);
    startTimerHz(30);
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgTrack));

    if (trackStrips.empty() && busStrips.empty()) {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(14.0f));
        g.drawText("No tracks", getLocalBounds(), juce::Justification::centred);
    }
}

void MixerView::resized() {
    auto area = getLocalBounds();
    int totalStrips = (int)trackStrips.size() + (int)busStrips.size();
    if (totalStrips == 0) return;

    int stripWidth = std::min(Theme::trackStripWidth,
                               area.getWidth() / std::max(1, totalStrips));

    for (auto& strip : trackStrips)
        strip->setBounds(area.removeFromLeft(stripWidth));
    for (auto& strip : busStrips)
        strip->setBounds(area.removeFromLeft(stripWidth));
}

void MixerView::timerCallback() {
    auto trackNames = api.listTrackNames();
    auto busNames = api.listBusNames();

    if (trackNames != lastTrackNames || busNames != lastBusNames) {
        lastTrackNames = trackNames;
        lastBusNames = busNames;
        rebuildStrips();
    } else {
        // Update tracks
        for (size_t i = 0; i < trackStrips.size() && i < lastTrackNames.size(); ++i) {
            auto& name = lastTrackNames[i];
            trackStrips[i]->setInstrumentName(api.getTrackPluginName(name));
            trackStrips[i]->setEffectNames(api.getTrackEffectNames(name));
            trackStrips[i]->setMidiEnabled(api.isTrackMidiEnabled(name));
            trackStrips[i]->setGain(api.getTrackGain(name));
            trackStrips[i]->setPeakLevel(api.getTrackPeakLevel(name));

            // Sends
            std::vector<SendsPanel::SendInfo> sends;
            for (auto& s : api.getTrackSends(name))
                sends.push_back({ s.busName, s.gain, 0.0f });  // TODO: send peak level
            trackStrips[i]->setSends(sends);
            trackStrips[i]->setAvailableBusses(lastBusNames);
        }
        // Update busses
        for (size_t i = 0; i < busStrips.size() && i < lastBusNames.size(); ++i) {
            auto& name = lastBusNames[i];
            busStrips[i]->setEffectNames(api.getBusEffectNames(name));
            busStrips[i]->setGain(api.getBusGain(name));
            busStrips[i]->setPeakLevel(api.getBusPeakLevel(name));
        }
    }
}

void MixerView::rebuildStrips() {
    trackStrips.clear();
    busStrips.clear();

    for (auto& trackName : lastTrackNames) {
        auto strip = std::make_unique<TrackStrip>(trackName, api);
        strip->setInstrumentName(api.getTrackPluginName(trackName));
        strip->setEffectNames(api.getTrackEffectNames(trackName));
        strip->setMidiEnabled(api.isTrackMidiEnabled(trackName));
        strip->setAvailableBusses(lastBusNames);

        std::vector<SendsPanel::SendInfo> sends;
        for (auto& s : api.getTrackSends(trackName))
            sends.push_back({ s.busName, s.gain, 0.0f });
        strip->setSends(sends);

        addAndMakeVisible(*strip);
        trackStrips.push_back(std::move(strip));
    }

    for (auto& busName : lastBusNames) {
        auto strip = std::make_unique<BusStrip>(busName, api);
        strip->setEffectNames(api.getBusEffectNames(busName));
        addAndMakeVisible(*strip);
        busStrips.push_back(std::move(strip));
    }

    resized();
}
