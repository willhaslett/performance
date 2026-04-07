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
    auto tracks = api.listTracks();
    auto busses = api.listBusses();

    // Detect changes by comparing IDs and names
    bool tracksChanged = (tracks.size() != lastTracks.size());
    if (!tracksChanged) {
        for (size_t i = 0; i < tracks.size(); ++i) {
            if (tracks[i].id != lastTracks[i].id || tracks[i].name != lastTracks[i].name) {
                tracksChanged = true;
                break;
            }
        }
    }
    bool bussesChanged = (busses.size() != lastBusses.size());
    if (!bussesChanged) {
        for (size_t i = 0; i < busses.size(); ++i) {
            if (busses[i].id != lastBusses[i].id || busses[i].name != lastBusses[i].name) {
                bussesChanged = true;
                break;
            }
        }
    }

    if (tracksChanged || bussesChanged) {
        lastTracks = tracks;
        lastBusses = busses;
        rebuildStrips();
    } else {
        // Build bus options for sends panels
        std::vector<SendsPanel::BusOption> busOptions;
        for (auto& b : lastBusses)
            busOptions.push_back({ b.id, b.name });

        // Update tracks
        for (size_t i = 0; i < trackStrips.size() && i < lastTracks.size(); ++i) {
            auto& id = lastTracks[i].id;
            trackStrips[i]->setInstrumentName(api.getTrackPluginName(id));
            trackStrips[i]->setEffects(api.getTrackEffects(id));
            trackStrips[i]->setMidiEnabled(api.isTrackMidiEnabled(id));
            trackStrips[i]->setGain(api.getTrackGain(id));
            trackStrips[i]->setPeakLevel(api.getTrackPeakLevel(id));

            // Sends
            std::vector<SendsPanel::SendInfo> sends;
            for (auto& s : api.getTrackSends(id))
                sends.push_back({ s.busName, s.busId, s.gain, s.peakLevel });
            trackStrips[i]->setSends(sends);
            trackStrips[i]->setAvailableBusses(busOptions);
        }
        // Update busses
        for (size_t i = 0; i < busStrips.size() && i < lastBusses.size(); ++i) {
            auto& id = lastBusses[i].id;
            busStrips[i]->setEffects(api.getBusEffects(id));
            busStrips[i]->setGain(api.getBusGain(id));
            busStrips[i]->setPeakLevel(api.getBusPeakLevel(id));
        }
    }

    // Update output strip
    outputStrip.setEffects(api.getMasterEffects());
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

    std::vector<SendsPanel::BusOption> busOptions;
    for (auto& b : lastBusses)
        busOptions.push_back({ b.id, b.name });

    for (auto& t : lastTracks) {
        auto strip = std::make_unique<TrackStrip>(t.id, t.name, api);
        strip->setInstrumentName(api.getTrackPluginName(t.id));
        strip->setEffects(api.getTrackEffects(t.id));
        strip->setMidiEnabled(api.isTrackMidiEnabled(t.id));
        strip->setAvailableBusses(busOptions);

        std::vector<SendsPanel::SendInfo> sends;
        for (auto& s : api.getTrackSends(t.id))
            sends.push_back({ s.busName, s.busId, s.gain, s.peakLevel });
        strip->setSends(sends);

        stripContainer.addAndMakeVisible(*strip);
        trackStrips.push_back(std::move(strip));
    }

    for (auto& b : lastBusses) {
        auto strip = std::make_unique<BusStrip>(b.id, b.name, api);
        strip->setEffects(api.getBusEffects(b.id));
        stripContainer.addAndMakeVisible(*strip);
        busStrips.push_back(std::move(strip));
    }

    resized();
}
