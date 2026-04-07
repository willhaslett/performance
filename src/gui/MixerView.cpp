#include "gui/MixerView.h"
#include "gui/SendsPanel.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"

MixerView::MixerView(StateAPI& state, EngineAPI& engine)
    : state(state), engine(engine), outputStrip(state, engine) {
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
    auto stateTracks = state.listTracks();
    auto stateBusses = state.listBusses();

    // Convert to local types for comparison
    std::vector<TrackInfo> tracks;
    for (auto& t : stateTracks)
        tracks.push_back({ juce::String(t.id), juce::String(t.name) });
    std::vector<BusInfo> busses;
    for (auto& b : stateBusses)
        busses.push_back({ juce::String(b.id), juce::String(b.name) });

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
            trackStrips[i]->setInstrumentName(juce::String(state.getTrackPluginName(id.toStdString())));

            // Convert StateAPI::EffectSlotInfo to TrackStrip::EffectSlotInfo
            auto stateEffects = state.getTrackEffects(id.toStdString());
            std::vector<TrackStrip::EffectSlotInfo> effects;
            for (auto& e : stateEffects)
                effects.push_back({ juce::String(e.effectId), juce::String(e.pluginName) });
            trackStrips[i]->setEffects(effects);

            trackStrips[i]->setMidiEnabled(state.isTrackMidiEnabled(id.toStdString()));
            trackStrips[i]->setGain(state.getTrackGain(id.toStdString()));
            trackStrips[i]->setPeakLevel(engine.getTrackPeakLevel(id));

            // Sends
            auto stateSends = state.getTrackSends(id.toStdString());
            std::vector<SendsPanel::SendInfo> sends;
            for (auto& s : stateSends)
                sends.push_back({ juce::String(s.busName), juce::String(s.busId), s.gain, 0.0f });
            trackStrips[i]->setSends(sends);
            trackStrips[i]->setAvailableBusses(busOptions);
        }
        // Update busses
        for (size_t i = 0; i < busStrips.size() && i < lastBusses.size(); ++i) {
            auto& id = lastBusses[i].id;

            auto stateEffects = state.getBusEffects(id.toStdString());
            std::vector<BusStrip::EffectSlotInfo> effects;
            for (auto& e : stateEffects)
                effects.push_back({ juce::String(e.effectId), juce::String(e.pluginName) });
            busStrips[i]->setEffects(effects);

            busStrips[i]->setGain(state.getBusGain(id.toStdString()));
            busStrips[i]->setPeakLevel(engine.getBusPeakLevel(id));
        }
    }

    // Update output strip
    auto stateMasterEffects = state.getMasterEffects();
    std::vector<OutputStrip::EffectSlotInfo> masterEffects;
    for (auto& e : stateMasterEffects)
        masterEffects.push_back({ juce::String(e.effectId), juce::String(e.pluginName) });
    outputStrip.setEffects(masterEffects);
    outputStrip.setGain(state.getMasterGain());
    outputStrip.setPeakLevel(engine.getMasterPeakLevel());

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
        auto strip = std::make_unique<TrackStrip>(t.id, t.name, state, engine);
        strip->setInstrumentName(juce::String(state.getTrackPluginName(t.id.toStdString())));

        auto stateEffects = state.getTrackEffects(t.id.toStdString());
        std::vector<TrackStrip::EffectSlotInfo> effects;
        for (auto& e : stateEffects)
            effects.push_back({ juce::String(e.effectId), juce::String(e.pluginName) });
        strip->setEffects(effects);

        strip->setMidiEnabled(state.isTrackMidiEnabled(t.id.toStdString()));
        strip->setAvailableBusses(busOptions);

        auto stateSends = state.getTrackSends(t.id.toStdString());
        std::vector<SendsPanel::SendInfo> sends;
        for (auto& s : stateSends)
            sends.push_back({ juce::String(s.busName), juce::String(s.busId), s.gain, 0.0f });
        strip->setSends(sends);

        stripContainer.addAndMakeVisible(*strip);
        trackStrips.push_back(std::move(strip));
    }

    for (auto& b : lastBusses) {
        auto strip = std::make_unique<BusStrip>(b.id, b.name, state, engine);

        auto stateEffects = state.getBusEffects(b.id.toStdString());
        std::vector<BusStrip::EffectSlotInfo> effects;
        for (auto& e : stateEffects)
            effects.push_back({ juce::String(e.effectId), juce::String(e.pluginName) });
        strip->setEffects(effects);

        stripContainer.addAndMakeVisible(*strip);
        busStrips.push_back(std::move(strip));
    }

    resized();
}
