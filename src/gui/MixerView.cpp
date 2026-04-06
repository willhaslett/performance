#include "gui/MixerView.h"
#include "gui/SendsPanel.h"
#include "api/PerformanceAPI.h"

MixerView::MixerView(PerformanceAPI& api) : api(api), outputStrip(api) {
    viewport.setViewedComponent(&stripContainer, false);
    viewport.setScrollBarsShown(false, true);  // horizontal only
    addAndMakeVisible(viewport);
    stripContainer.addAndMakeVisible(outputStrip);
    startTimerHz(30);
}

int MixerView::getDesiredHeight() const {
    constexpr int minMixerHeight = 200;
    int maxH = outputStrip.getMinimumHeight();
    for (auto& s : trackStrips)
        maxH = std::max(maxH, s->getMinimumHeight());
    for (auto& s : busStrips)
        maxH = std::max(maxH, s->getMinimumHeight());
    return std::max(minMixerHeight, maxH);
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgTrack));

    // Top border
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, 0.0f, (float)getWidth(), 0.0f, 1.0f);

    if (trackStrips.empty() && busStrips.empty()) {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(14.0f));
        auto msgArea = getLocalBounds().withTrimmedRight(Theme::trackStripWidth);
        g.drawText("No tracks", msgArea, juce::Justification::centred);
    }
}

void MixerView::resized() {
    auto area = getLocalBounds();
    viewport.setBounds(area.withTrimmedTop(1));  // 1px for border

    // All strips (tracks + busses + output) in the scrollable container
    int totalStrips = (int)trackStrips.size() + (int)busStrips.size() + 1;  // +1 for output
    int stripWidth = Theme::trackStripWidth;
    int totalWidth = totalStrips * stripWidth;
    int stripHeight = area.getHeight();

    stripContainer.setSize(std::max(totalWidth, area.getWidth()), stripHeight);

    int x = 0;
    for (auto& strip : trackStrips) {
        strip->setBounds(x, 0, stripWidth, stripHeight);
        x += stripWidth;
    }
    for (auto& strip : busStrips) {
        strip->setBounds(x, 0, stripWidth, stripHeight);
        x += stripWidth;
    }
    outputStrip.setBounds(x, 0, stripWidth, stripHeight);
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
                sends.push_back({ s.busName, s.gain, s.peakLevel });
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

    // Update output strip
    outputStrip.setEffectNames(api.getMasterEffectNames());
    outputStrip.setGain(api.getMasterGain());
    outputStrip.setPeakLevel(api.getMasterPeakLevel());

    // If desired height changed, trigger parent re-layout
    int h = getDesiredHeight();
    if (h != lastDesiredHeight) {
        lastDesiredHeight = h;
        if (auto* parent = getParentComponent())
            parent->resized();
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
            sends.push_back({ s.busName, s.gain, s.peakLevel });
        strip->setSends(sends);

        stripContainer.addAndMakeVisible(*strip);
        trackStrips.push_back(std::move(strip));
    }

    for (auto& busName : lastBusNames) {
        auto strip = std::make_unique<BusStrip>(busName, api);
        strip->setEffectNames(api.getBusEffectNames(busName));
        stripContainer.addAndMakeVisible(*strip);
        busStrips.push_back(std::move(strip));
    }

    resized();
}
