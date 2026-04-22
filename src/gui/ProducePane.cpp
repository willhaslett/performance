#include "gui/ProducePane.h"
#include "gui/MorphEditor.h"
#include "gui/ActionInstanceForm.h"
#include "gui/ActionPicker.h"
#include "gui/TrackUi.h"
#include "api/StateAPI.h"
#include "state/StateEvents.h"
#include "engine/Log.h"
#include <map>

ProducePane::ProducePane() {
    setWantsKeyboardFocus(true);

    // Metronome volume slider
    metronomeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    metronomeSlider.setRange(0.0, 1.0, 0.01);
    metronomeSlider.setValue(0.5);
    metronomeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    metronomeSlider.setColour(juce::Slider::trackColourId, Theme::color(Theme::Color::bgSlot));
    metronomeSlider.setColour(juce::Slider::thumbColourId, Theme::color(Theme::Color::textSecondary));
    metronomeSlider.setColour(juce::Slider::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    metronomeSlider.onValueChange = [this]() {
        if (state) {
            state->setConfig("metronome_volume", std::to_string(metronomeSlider.getValue()));
        }
    };
    addAndMakeVisible(metronomeSlider);

    metronomeLabel.setText("Met", juce::dontSendNotification);
    metronomeLabel.setFont(Theme::font(Theme::fontSizeXs));
    metronomeLabel.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textDim));
    metronomeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(metronomeLabel);

    startTimerHz(30);
}

ProducePane::~ProducePane() {
    if (state && stateSubscriptionId >= 0)
        state->events().unsubscribe(stateSubscriptionId);
}

void ProducePane::setState(StateAPI* s, SequencerAPI* seq, Arrangement* arr) {
    state = s;
    sequencer = seq;
    arrangement = arr;

    if (state) {
        // Load saved metronome volume
        auto metVol = state->getConfig("metronome_volume");
        if (!metVol.empty())
            metronomeSlider.setValue(std::stof(metVol), juce::dontSendNotification);

        // Restore zoom state
        auto ppb = state->getConfig("zoom_pixels_per_beat");
        if (!ppb.empty()) pixelsPerBeat = std::stod(ppb);
        auto trh = state->getConfig("zoom_track_row_height");
        if (!trh.empty()) trackRowHeight = std::max(72, std::stoi(trh));

        stateSubscriptionId = state->events().subscribe([this](const StateEvent& event) {
            if (event.entity == StateEvent::Track || event.entity == StateEvent::Config)
                juce::MessageManager::callAsync([this] { repaint(); });
        });
    }
}

void ProducePane::timerCallback() {
    if (sequencer && sequencer->isPlaying()) {
        // Auto-scroll (Logic-style page scroll):
        // When playhead reaches ~1 bar before the right edge, jump forward
        // by the full visible beat range so bar positions stay stable.
        double beat = sequencer->getBeatPosition();
        int gridWidth = getWidth() - trackHeaderWidth;
        double visibleBeats = gridWidth / pixelsPerBeat;
        double rightEdgeBeat = scrollBeat + visibleBeats;
        double threshold = rightEdgeBeat - beatsPerBar();
        if (beat >= threshold) {
            // Snap scroll position to bar boundary for visual stability
            int bpb = beatsPerBar();
            double newScroll = std::floor(beat / bpb) * bpb;
            scrollBeat = std::max(0.0, newScroll);
        }
    }
    repaint();
}

int ProducePane::beatsPerBar() const {
    jassert(sequencer != nullptr);
    return sequencer->getTimeSignatureNumerator();
}

int ProducePane::beatToX(double beat) const {
    return trackHeaderWidth + (int)((beat - scrollBeat) * pixelsPerBeat);
}

double ProducePane::xToBeat(int x) const {
    return scrollBeat + (double)(x - trackHeaderWidth) / pixelsPerBeat;
}

void ProducePane::showActionPicker(juce::Point<int> screenPos, const std::string& trackId, double beat) {
    if (!state) return;
    auto trkId = trackId;
    auto evBeat = beat;
    ActionPicker::launch(*state, screenPos, /*filter*/ {},
        [this, trkId, evBeat](const ActionInfo& action, const juce::var& args) {
            // Morph: route to the dedicated MorphEditor (richer compound UI).
            // The form's morph button would open the same editor, but our
            // legacy path stores the morph blob unwrapped — keep that path
            // until we unify the storage format.
            if (action.name == "morph") {
                showMorphEditor(trkId, evBeat);
                return;
            }
            auto* ts = state ? state->findTrack(TrackId{trkId}) : nullptr;
            if (!ts) {
                perfLog("[ProducePane] action create: track '%s' not found\n", trkId.c_str());
                return;
            }
            ActionEventData ae;
            ae.id = ActionEventId{juce::Uuid().toString().toStdString()};
            ae.beat = evBeat;
            ae.actionId = action.id;
            ae.argsJson = juce::JSON::toString(args, true).toStdString();
            perfLog("[ProducePane] action created: track='%s' beat=%.3f action='%s' args=%s (now %d events on track)\n",
                    trkId.c_str(), evBeat, action.name.c_str(),
                    ae.argsJson.c_str(), (int)ts->actionData.size() + 1);
            ts->actionData.push_back(std::move(ae));
            state->markDirty();
            repaint();
        });
}

void ProducePane::showMorphEditor(const std::string& trackId, double beat,
                                   const std::string& existingEventId) {
    if (!state) return;

    auto* editor = new MorphEditor(*state);

    // Load existing data if editing
    if (!existingEventId.empty()) {
        auto* ts = state->findTrack(TrackId{trackId});
        if (ts) {
            for (auto& ae : ts->actionData) {
                if (ae.id.str() == existingEventId) {
                    editor->setMorphData(juce::JSON::parse(juce::String(ae.argsJson)));
                    break;
                }
            }
        }
    }

    auto trkId = trackId;
    auto evId = existingEventId;
    auto evBeat = beat;

    // Show as a popup window with working close button
    struct MorphWindow : public juce::DocumentWindow {
        std::function<void()> onClose;
        MorphWindow() : DocumentWindow("Morph", Theme::color(Theme::Color::bgOverlay), closeButton) {}
        void closeButtonPressed() override { if (onClose) onClose(); }
    };
    auto* window = new MorphWindow();
    window->setContentOwned(editor, true);
    window->centreWithSize(editor->getWidth(), editor->getHeight());
    window->setUsingNativeTitleBar(false);
    window->setVisible(true);
    window->setAlwaysOnTop(true);

    editor->onDone = [this, window, editor, trkId, evId, evBeat]() {
        if (!state) { delete window; return; }
        auto morphData = editor->getMorphData();
        auto json = juce::JSON::toString(morphData, true).toStdString();

        ActionId morphActionId;
        for (auto& a : state->allActions())
            if (a.name == "morph") { morphActionId = a.id; break; }

        auto* ts = state->findTrack(TrackId{trkId});
        if (ts && !morphActionId.empty()) {
            if (!evId.empty()) {
                for (auto& ae : ts->actionData) {
                    if (ae.id.str() == evId) {
                        ae.argsJson = json;
                        ae.actionId = morphActionId;
                        break;
                    }
                }
            } else {
                ActionEventData ae;
                ae.id = ActionEventId{juce::Uuid().toString().toStdString()};
                ae.beat = evBeat;
                ae.actionId = morphActionId;
                ae.argsJson = json;
                ts->actionData.push_back(std::move(ae));
            }
            state->markDirty();
        }
        delete window;
        repaint();
    };
    editor->onCancel = [window]() { delete window; };
    window->onClose = editor->onCancel;
}

void ProducePane::saveZoomState() {
    if (state) {
        state->setConfig("zoom_pixels_per_beat", std::to_string(pixelsPerBeat));
        state->setConfig("zoom_track_row_height", std::to_string(trackRowHeight));
    }
}

double ProducePane::snapBeatToGrid(double beat) const {
    if (!snapToGrid || !sequencer) return beat;
    double divSize = 1.0 / sequencer->getTimeSignatureDenominator();
    return std::round(beat / divSize) * divSize;
}

void ProducePane::ensurePlayheadVisible() {
    if (!sequencer) return;
    double beat = sequencer->getBeatPosition();
    int gridWidth = getWidth() - trackHeaderWidth;
    double visibleBeats = gridWidth / pixelsPerBeat;
    if (beat < scrollBeat || beat > scrollBeat + visibleBeats) {
        // Snap scroll so playhead is near the left edge, aligned to bar
        int bpb = beatsPerBar();
        scrollBeat = std::max(0.0, std::floor(beat / bpb) * bpb);
    }
}

// --- Paint ---

void ProducePane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    auto area = getLocalBounds();

    // Transport bar at top
    auto transportArea = area.removeFromTop(transportHeight);
    paintTransport(g, transportArea);

    // Ruler below transport
    auto rulerArea = area.removeFromTop(rulerHeight);
    paintRuler(g, rulerArea);

    // Track headers on left, grid on right
    auto trackArea = area.removeFromLeft(trackHeaderWidth);
    paintTrackHeaders(g, trackArea);
    {
        juce::Graphics::ScopedSaveState sss(g);
        paintGrid(g, area);
    }

    // Drag reorder indicator
    if (state && dragTrackIndex >= 0 && dragTargetIndex >= 0
        && dragTrackIndex != dragTargetIndex) {
        int gridTop = transportHeight + rulerHeight;
        auto tracks = state->listTracks();
        auto heightAt = [&](int idx) {
            if (idx < 0 || idx >= (int)tracks.size()) return trackRowHeight;
            auto* ts = state->findTrack(tracks[idx].id);
            return ts ? rowHeightFor(*ts) : trackRowHeight;
        };

        int indicatorY = gridTop + rowYFor((size_t)dragTargetIndex);
        if (dragTargetIndex > dragTrackIndex)
            indicatorY += heightAt(dragTargetIndex);

        // Full-width indicator line
        g.setColour(Theme::color(Theme::Color::accent));
        g.fillRect(0, indicatorY - 2, getWidth(), 5);

        // Dim the source track row
        int srcY = gridTop + rowYFor((size_t)dragTrackIndex);
        g.setColour(Theme::color(Theme::Color::overlayDim));
        g.fillRect(0, srcY, getWidth(), heightAt(dragTrackIndex));
    }

    // Playhead overlaid
    if (sequencer) {
        auto fullGridArea = getLocalBounds()
            .withTrimmedTop(transportHeight + rulerHeight)
            .withTrimmedLeft(trackHeaderWidth);
        // Also draw playhead on ruler
        auto fullRulerGrid = getLocalBounds()
            .withTrimmedTop(transportHeight)
            .withTrimmedLeft(trackHeaderWidth);
        paintPlayhead(g, fullRulerGrid);
    }

    // Right border — drawn last so track lane fills don't overdraw it
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)getWidth() - 1, 0.0f, (float)getWidth() - 1, (float)getHeight(), 1.0f);
}

void ProducePane::paintTransportButton(juce::Graphics& g, juce::Rectangle<int> bounds,
                                        TransportGlyph glyph, bool active,
                                        bool hovered, juce::Colour activeCol) {
    const float cornerR = 4.0f;
    auto boundsF = bounds.toFloat();

    // Container — buttons live inside a shared group container, so at rest
    // they paint nothing. Active gets a colored pill; hover gets a subtle
    // brighten on top of the group bg.
    if (active) {
        g.setColour(hovered ? activeCol.brighter(0.15f) : activeCol);
        g.fillRoundedRectangle(boundsF, cornerR);
    } else if (hovered) {
        g.setColour(Theme::color(Theme::Color::bgControlHover));
        g.fillRoundedRectangle(boundsF, cornerR);
    }

    // Glyph colour
    juce::Colour glyphCol;
    if (active)
        glyphCol = Theme::color(Theme::Color::textOnColor);
    else
        glyphCol = hovered ? Theme::color(Theme::Color::textPrimary)
                            : Theme::color(Theme::Color::textSecondary);
    g.setColour(glyphCol);

    // Glyph — all filled shapes for uniform weight, except Cycle which is
    // stroked to read as a flow/path symbol.
    auto inner = boundsF.reduced(7.0f);

    switch (glyph) {
    case TransportGlyph::Rewind: {
        // |◀◀  — bar on left, two left-pointing triangles
        const float barW = 1.5f;
        const float gap = 1.0f;
        const float triW = (inner.getWidth() - barW - gap) * 0.5f;
        g.fillRect(inner.getX(), inner.getY(), barW, inner.getHeight());
        juce::Path tri1, tri2;
        float t1X = inner.getX() + barW + gap;
        tri1.addTriangle(t1X, inner.getCentreY(),
                         t1X + triW, inner.getY(),
                         t1X + triW, inner.getBottom());
        float t2X = t1X + triW;
        tri2.addTriangle(t2X, inner.getCentreY(),
                         t2X + triW, inner.getY(),
                         t2X + triW, inner.getBottom());
        g.fillPath(tri1);
        g.fillPath(tri2);
        break;
    }
    case TransportGlyph::Stop:
        g.fillRect(boundsF.reduced(8.0f));
        break;
    case TransportGlyph::Play: {
        juce::Path tri;
        tri.addTriangle(inner.getX(), inner.getY(),
                        inner.getX(), inner.getBottom(),
                        inner.getRight(), inner.getCentreY());
        g.fillPath(tri);
        break;
    }
    case TransportGlyph::Record:
        g.fillEllipse(boundsF.reduced(7.0f));
        break;
    case TransportGlyph::Cycle: {
        // Two interlocking arcs forming a loop (Logic-style), drawn as filled
        // closed paths so they match the visual weight of the other glyphs.
        // Each arc is a band (outer radius → inner radius) with a butt cap at
        // the sweep start and a tapered arrowhead at the sweep end.
        auto r = boundsF.reduced(4.0f);
        float cx = r.getCentreX();
        float cy = r.getCentreY();
        float rx = r.getWidth() * 0.42f;
        float ry = r.getHeight() * 0.32f;
        constexpr float pi = juce::MathConstants<float>::pi;

        const float thick   = 2.2f;   // band thickness
        const float headLen = 4.0f;   // arrowhead projection beyond the arc end
        const float gap     = 0.55f;  // half-gap angle between the two arcs

        auto halfLoop = [&](float startA, float endA) {
            float rxO = rx + thick * 0.5f, ryO = ry + thick * 0.5f;
            float rxI = rx - thick * 0.5f, ryI = ry - thick * 0.5f;

            juce::Path p;
            // Outer-edge start (butt end)
            p.startNewSubPath(cx + rxO * std::cos(startA),
                              cy + ryO * std::sin(startA));
            // Outer arc along the sweep
            p.addCentredArc(cx, cy, rxO, ryO, 0.0f, startA, endA, false);
            // Arrowhead tip — project along tangent direction of increasing angle
            float tangX = -rx * std::sin(endA);
            float tangY =  ry * std::cos(endA);
            float tLen  = std::sqrt(tangX * tangX + tangY * tangY);
            float dx = tangX / tLen, dy = tangY / tLen;
            float tipX = cx + rx * std::cos(endA) + dx * headLen;
            float tipY = cy + ry * std::sin(endA) + dy * headLen;
            p.lineTo(tipX, tipY);
            // Return to inner edge at the same angular endpoint
            p.lineTo(cx + rxI * std::cos(endA),
                     cy + ryI * std::sin(endA));
            // Inner arc back
            p.addCentredArc(cx, cy, rxI, ryI, 0.0f, endA, startA, false);
            // Close across the butt
            p.closeSubPath();
            g.fillPath(p);
        };

        halfLoop(-pi + gap, -gap);   // top arc, arrowhead at right
        halfLoop(gap, pi - gap);     // bottom arc, arrowhead at left
        break;
    }
    }
}

void ProducePane::paintTransport(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(area);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)area.getBottom(), (float)getWidth(), (float)area.getBottom(), 1.0f);

    if (!sequencer) return;

    bool playing = sequencer->isPlaying();
    bool looping = sequencer->isLoopEnabled();
    double bpm = sequencer->getTempo();
    double beat = sequencer->getBeatPosition();

    // Layout: transport buttons left-aligned at the edge; LCD centered on
    // the full pane width.
    int btnSize = 28;
    int btnGap = 4;
    int lcdWidth = 520;
    int numButtons = 5;
    int totalButtonsW = numButtons * btnSize + (numButtons - 1) * btnGap;

    const int groupPad = 4;
    int btnX = Theme::spacingM + groupPad;              // left-align, leave the group container's own padding visible
    int lcdLeft = (getWidth() - lcdWidth) / 2;          // center on full pane width

    int btnY = area.getCentreY() - btnSize / 2;

    // --- Transport buttons ---

    bool inRecordMode = onIsRecordMode ? onIsRecordMode() : false;

    // Group container — a single rounded-rect behind all five buttons,
    // Logic-style. Individual buttons paint glyph-only at rest; active /
    // hover paint their own colored pill inside this container.
    auto groupBounds = juce::Rectangle<int>(
        btnX - groupPad, btnY - groupPad,
        totalButtonsW + 2 * groupPad + 4,  // +4 accounts for the cycle-button extra gap
        btnSize + 2 * groupPad);
    g.setColour(Theme::color(Theme::Color::bgControl));
    g.fillRoundedRectangle(groupBounds.toFloat(), 6.0f);

    rewindButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    paintTransportButton(g, rewindButtonBounds, TransportGlyph::Rewind,
                         false, hoveredTransport == HoveredTransport::Rewind,
                         juce::Colour());
    btnX += btnSize + btnGap;

    stopButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    paintTransportButton(g, stopButtonBounds, TransportGlyph::Stop,
                         false, hoveredTransport == HoveredTransport::Stop,
                         juce::Colour());
    btnX += btnSize + btnGap;

    playButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    paintTransportButton(g, playButtonBounds, TransportGlyph::Play,
                         playing, hoveredTransport == HoveredTransport::Play,
                         Theme::color(Theme::Color::transportPlay));
    btnX += btnSize + btnGap;

    recordButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    paintTransportButton(g, recordButtonBounds, TransportGlyph::Record,
                         inRecordMode, hoveredTransport == HoveredTransport::Record,
                         Theme::color(Theme::Color::transportRec));
    btnX += btnSize + btnGap + 4;

    cycleButtonBounds = juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
    paintTransportButton(g, cycleButtonBounds, TransportGlyph::Cycle,
                         looping, hoveredTransport == HoveredTransport::Cycle,
                         Theme::color(Theme::Color::accent));

    // --- Position display (LCD centered on grid area) ---
    int lcdX = lcdLeft;
    int lcdY = area.getY() + 8;
    int lcdHeight = area.getHeight() - 16;
    auto lcdBounds = juce::Rectangle<int>(lcdX, lcdY, lcdWidth, lcdHeight);

    // LCD background — interactive control base
    auto lcdBg = Theme::color(Theme::Color::bgControl);
    auto lcdBorder = Theme::color(Theme::Color::border);
    auto lcdDigit = Theme::color(Theme::Color::lcdDigit);
    g.setColour(lcdBg);
    g.fillRoundedRectangle(lcdBounds.toFloat(), 4.0f);
    g.setColour(lcdBorder);
    g.drawRoundedRectangle(lcdBounds.toFloat(), 4.0f, 1.0f);

    // Shared layout
    int digitTop = lcdBounds.getY() + 2;
    int digitH = lcdBounds.getHeight() - 14;
    int labelY = lcdBounds.getBottom() - 13;
    auto monoLg = Theme::fontMono(29.0f);
    auto monoMd = Theme::fontMono(23.0f);
    auto labelFont = Theme::font(8.0f);
    char buf[16];

    // --- Beat position: BAR . BEAT . DIV . TICK ---
    int bar = (int)(beat / beatsPerBar()) + 1;
    int beatInBar = (int)std::fmod(beat, (double)beatsPerBar()) + 1;
    double fractional = std::fmod(beat, 1.0);
    int tsDen = sequencer->getTimeSignatureDenominator();
    int div = (int)(fractional * tsDen) + 1;
    int tick = (int)(std::fmod(fractional * tsDen, 1.0) * 240);

    int colX = lcdBounds.getX() + 6;

    auto drawCol = [&](const char* text, const char* label, int width, juce::Font font) {
        g.setFont(font);
        g.setColour(lcdDigit);
        g.drawText(text, colX, digitTop, width, digitH, juce::Justification::centred);
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(labelFont);
        g.drawText(label, colX, labelY, width, 12, juce::Justification::centred);
        colX += width;
    };

    auto drawSep = [&]() {
        g.setColour(lcdBorder);
        g.drawLine((float)colX, (float)(lcdBounds.getY() + 4),
                   (float)colX, (float)(lcdBounds.getBottom() - 4), 1.0f);
        colX += 6;
    };

    snprintf(buf, sizeof(buf), "%3d", bar);
    drawCol(buf, "BAR", 58, monoLg);
    snprintf(buf, sizeof(buf), "%d", beatInBar);
    drawCol(buf, "BEAT", 38, monoLg);
    snprintf(buf, sizeof(buf), "%d", div);
    drawCol(buf, "DIV", 32, monoLg);
    snprintf(buf, sizeof(buf), "%03d", tick);
    drawCol(buf, "TICK", 52, monoLg);

    drawSep();

    // --- Time: HH : MM : SS . ms ---
    double totalSeconds = (bpm > 0) ? (beat / (bpm / 60.0)) : 0.0;
    int hrs = (int)(totalSeconds / 3600.0);
    int mins = (int)(std::fmod(totalSeconds, 3600.0) / 60.0);
    int secs = (int)std::fmod(totalSeconds, 60.0);
    int ms = (int)(std::fmod(totalSeconds, 1.0) * 1000.0);

    snprintf(buf, sizeof(buf), "%d:%02d:%02d.%03d", hrs, mins, secs, ms);
    drawCol(buf, "TIME", 180, monoMd);

    drawSep();

    // --- Tempo + Time Signature (clickable) ---
    bpmClickBounds = juce::Rectangle<int>(colX, lcdBounds.getY(), 52, lcdBounds.getHeight());
    snprintf(buf, sizeof(buf), "%.1f", bpm);
    drawCol(buf, "BPM", 64, monoMd);

    colX += 2;
    timeSigClickBounds = juce::Rectangle<int>(colX, lcdBounds.getY(), 48, lcdBounds.getHeight());
    snprintf(buf, sizeof(buf), "%d/%d",
             sequencer->getTimeSignatureNumerator(), tsDen);
    drawCol(buf, "TIME SIG", 58, monoMd);
}

void ProducePane::paintRuler(juce::Graphics& g, juce::Rectangle<int> area) {
    auto rulerArea = area.withLeft(trackHeaderWidth);
    g.setColour(Theme::color(Theme::Color::bgApp));
    g.fillRect(rulerArea);
    auto rulerLineCol = Theme::color(Theme::Color::border).brighter(0.15f);
    g.setColour(rulerLineCol);
    g.drawLine((float)rulerArea.getX(), (float)rulerArea.getBottom(),
               (float)rulerArea.getRight(), (float)rulerArea.getBottom(), 1.0f);

    if (!sequencer) return;

    int bpb = beatsPerBar();
    int gridWidth = getWidth() - trackHeaderWidth;
    double startBeat = scrollBeat;
    double endBeat = startBeat + gridWidth / pixelsPerBeat;

    int startBar = (int)(startBeat / bpb);
    int endBar = (int)(endBeat / bpb) + 1;

    // Adaptive bar number interval based on zoom
    double pixelsPerBar = pixelsPerBeat * bpb;
    int barLabelEvery = 1;
    if (pixelsPerBar < 12) barLabelEvery = 16;
    else if (pixelsPerBar < 24) barLabelEvery = 8;
    else if (pixelsPerBar < 48) barLabelEvery = 4;
    else if (pixelsPerBar < 80) barLabelEvery = 2;

    int tickBottom = rulerArea.getBottom() - 1;
    int tallTick = rulerArea.getHeight() - 4;   // bar lines
    int medTick = rulerArea.getHeight() / 2;     // beat lines
    int shortTick = rulerArea.getHeight() / 4;   // sub-beat ticks

    // Determine sub-beat resolution based on zoom
    int tsDen = sequencer->getTimeSignatureDenominator();
    double pixelsPerBeatVal = pixelsPerBeat;
    int subsPerBeat = 0;
    if (pixelsPerBeatVal > 60) subsPerBeat = tsDen;      // show subdivisions
    else if (pixelsPerBeatVal > 30) subsPerBeat = 2;     // half-beat ticks
    // else: no sub-beat ticks

    for (int bar = startBar; bar <= endBar; ++bar) {
        double barBeat = bar * bpb;

        for (int b = 0; b < bpb; ++b) {
            double beat = barBeat + b;
            int x = beatToX(beat);
            if (x < trackHeaderWidth || x > getWidth()) continue;

            if (b == 0) {
                // Bar line — tall tick
                g.setColour(rulerLineCol);
                g.drawLine((float)x, (float)(tickBottom - tallTick), (float)x, (float)tickBottom, 1.0f);

                // Bar number
                if ((bar + 1) % barLabelEvery == 0 || barLabelEvery == 1) {
                    g.setFont(Theme::font(Theme::fontSizeSm));
                    g.setColour(rulerLineCol);
                    g.drawText(juce::String(bar + 1), x + 3, rulerArea.getY(), 40, 16,
                               juce::Justification::centredLeft);
                }
            } else {
                // Beat line — medium tick
                g.setColour(Theme::color(Theme::Color::textDim));
                g.drawLine((float)x, (float)(tickBottom - medTick), (float)x, (float)tickBottom, 0.5f);
            }

            // Sub-beat ticks
            if (subsPerBeat > 0) {
                for (int s = 1; s < subsPerBeat; ++s) {
                    double subBeat = beat + (double)s / subsPerBeat;
                    int sx = beatToX(subBeat);
                    if (sx < trackHeaderWidth || sx > getWidth()) continue;
                    g.setColour(Theme::color(Theme::Color::textDim).withAlpha(0.4f));
                    g.drawLine((float)sx, (float)(tickBottom - shortTick), (float)sx, (float)tickBottom, 0.5f);
                }
            }
        }
    }

    // Cycle region highlight in ruler (full height)
    if (sequencer) {
        double loopStart = sequencer->getLoopStart();
        double loopEnd = sequencer->getLoopEnd();
        if (loopEnd > loopStart) {
            int x1 = std::max(beatToX(loopStart), trackHeaderWidth);
            int x2 = std::min(beatToX(loopEnd), getWidth());
            if (x2 > x1) {
                bool active = sequencer->isLoopEnabled();
                auto cycleCol = active ? Theme::color(Theme::Color::transportCycle)
                                       : Theme::color(Theme::Color::transportCycleOff);
                g.setColour(cycleCol.withAlpha(active ? 0.35f : 0.15f));
                g.fillRect(x1, rulerArea.getY(), x2 - x1, rulerArea.getHeight());
                g.setColour(cycleCol.withAlpha(0.6f));
                g.drawLine((float)x1, (float)rulerArea.getY(), (float)x1,
                           (float)rulerArea.getBottom(), 1.5f);
                g.drawLine((float)x2, (float)rulerArea.getY(), (float)x2,
                           (float)rulerArea.getBottom(), 1.5f);
            }
        }
    }
}

ProducePane::TrackRowVisuals ProducePane::trackRowVisuals(const TrackState& t) const {
    TrackRowVisuals v;
    v.audibility = t.muted ? Audibility::Muted : Audibility::Active;
    v.type       = t.sourceType;
    if (state) {
        auto sel = state->selectedTrackIds();
        v.selected = std::find(sel.begin(), sel.end(), t.id) != sel.end();
        v.focused  = state->getFocusedTrackId() == t.id;
    } else {
        v.selected = false;
        v.focused  = false;
    }
    return v;
}

ProducePane::RegionVisuals ProducePane::regionVisuals(const TrackState& t,
                                                      const RegionState& r) const {
    RegionVisuals v;
    v.audibility   = (t.muted || r.muted) ? Audibility::Muted : Audibility::Active;
    v.selected     = selectedRegionIds.count(r.id) > 0;
    v.beingDragged = draggingRegion && r.id == dragRegionId;
    v.beingTrimmed = trimEdge != TrimEdge::None && r.id == trimRegionId;
    return v;
}

void ProducePane::paintTrackRow(juce::Graphics& g, juce::Rectangle<int> bounds,
                                const TrackRowVisuals& v) {
    // Flat bg only. Focus halo is painted separately at the header
    // call-site (paintTrackHeaders) — NOT here — because this function
    // also paints the timeline-lane bg (paintGrid), where a gradient
    // would be wrong.
    TrackUi::paintTrackBgFlat(g, bounds, v.audibility == Audibility::Muted,
                               v.focused, v.selected);
}

int ProducePane::rowHeightFor(const TrackState& t) const {
    return t.sourceType == TrackSourceType::Action
            ? std::max(16, trackRowHeight / 2)
            : trackRowHeight;
}

int ProducePane::rowYFor(size_t trackIndex) const {
    if (!state) return 0;
    auto tracks = state->listTracks();
    int y = 0;
    for (size_t i = 0; i < trackIndex && i < tracks.size(); ++i) {
        auto* ts = state->findTrack(tracks[i].id);
        y += ts ? rowHeightFor(*ts) : trackRowHeight;
    }
    return y;
}

juce::Colour ProducePane::regionFillColour(Audibility a) const {
    auto base = Theme::color(Theme::Color::bgSurfaceRaised);
    return a == Audibility::Muted ? base.darker(0.5f) : base;
}

void ProducePane::paintRegionShell(juce::Graphics& g, juce::Rectangle<int> bounds,
                                   const RegionVisuals& v) {
    float baseAlpha = v.beingDragged ? 0.45f : 0.82f;
    g.setColour(regionFillColour(v.audibility).withAlpha(baseAlpha));
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);

    g.setColour(v.selected ? Theme::color(Theme::Color::accent)
                            : Theme::color(Theme::Color::border));
    g.drawRoundedRectangle(bounds.toFloat(), 5.0f, v.selected ? 2.0f : 1.0f);
}

void ProducePane::paintTrackHeaders(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(area);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getRight(), (float)area.getY(),
               (float)area.getRight(), (float)area.getBottom(), 1.0f);

    if (!state) return;

    int y = area.getY();

    auto tracks = state->listTracks();
    muteBounds.resize(tracks.size());
    soloBounds.resize(tracks.size());
    armBounds.resize(tracks.size());
    inputMonitorBounds.resize(tracks.size());
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto& t = tracks[i];
        auto* trackState = state->findTrack(t.id);
        if (!trackState) { y += trackRowHeight; continue; }

        int rowH = rowHeightFor(*trackState);
        auto row = juce::Rectangle<int>(area.getX(), y, area.getWidth(), rowH);

        auto vis = trackRowVisuals(*trackState);
        paintTrackRow(g, row, vis);

        // Type accent — left-edge stripe. Thickens on focus as the
        // "this is the track I'm playing into" affordance.
        // Action tracks get no stripe.
        if (vis.type != TrackSourceType::Action) {
            auto accentColor = vis.type == TrackSourceType::AudioInput
                                ? Theme::Color::typeAudio
                                : Theme::Color::typeInstrument;
            int thickness = vis.focused ? 4 : 3;
            g.setColour(Theme::color(accentColor));
            g.fillRect(row.getX(), row.getY(), thickness, row.getHeight());
        }

        // Row separators
        g.setColour(Theme::color(Theme::Color::border));
        if (i == 0)
            g.drawLine((float)area.getX(), (float)y,
                       (float)area.getRight(), (float)y, 0.5f);
        g.drawLine((float)area.getX(), (float)(y + rowH),
                   (float)area.getRight(), (float)(y + rowH), 0.5f);

        // Row 1: track name
        // Uses Theme::headerHeight so the name block matches mixer strip headers.
        int row1Y = y + 4;
        int row1H = Theme::headerHeight;
        int cx = area.getX() + 8;

        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(Theme::fontSizeLg));
        int nameRight = area.getRight() - 4;
        g.drawText(juce::String(t.name), area.getX() + Theme::spacingM, row1Y, nameRight - (area.getX() + Theme::spacingM), row1H,
                   juce::Justification::centredLeft);

        // Row 2: pill buttons (M S R I) — 10px gap below name row
        int row2Y = row1Y + row1H + 10;
        int cy_row = row2Y + Theme::pillSize / 2;
        cx = area.getX() + 8;

        auto drawPill = [&](juce::Rectangle<int>& bounds, const char* label,
                            bool active, uint32_t activeColor, bool hovered) {
            bounds = juce::Rectangle<int>(cx, cy_row - Theme::pillSize / 2, Theme::pillSize, Theme::pillSize);
            if (active) {
                g.setColour(Theme::color(activeColor));
                g.fillRoundedRectangle(bounds.toFloat(), Theme::pillRadius);
                g.setColour(Theme::color(Theme::Color::textOnColor));
            } else {
                g.setColour(Theme::color(hovered ? Theme::Color::bgControlHover
                                                 : Theme::Color::bgControl));
                g.fillRoundedRectangle(bounds.toFloat(), Theme::pillRadius);
                g.setColour(Theme::color(Theme::Color::pillTextOff));
            }
            g.setFont(Theme::font(Theme::fontSizePill));
            g.drawText(label, bounds, juce::Justification::centred);
            cx += Theme::pillSize + Theme::pillGap;
        };

        bool isActionTrack = vis.type == TrackSourceType::Action;
        bool isHoveredRow = ((int)i == hoveredPillTrackIdx);

        muteBounds[i] = {};
        soloBounds[i] = {};
        if (!isActionTrack) {
            drawPill(muteBounds[i], "M", trackState->muted, Theme::Color::pillMute,
                     isHoveredRow && hoveredPill == HoveredPill::Mute);
            drawPill(soloBounds[i], "S", trackState->soloed, Theme::Color::pillSolo,
                     isHoveredRow && hoveredPill == HoveredPill::Solo);
            cx += Theme::pillGroupGap - Theme::pillGap;
        }

        armBounds[i] = {};
        if (!isActionTrack) {
            drawPill(armBounds[i], "R", trackState->armed, Theme::Color::pillArm,
                     isHoveredRow && hoveredPill == HoveredPill::Arm);
        }

        inputMonitorBounds[i] = {};
        if (vis.type == TrackSourceType::AudioInput
            || vis.type == TrackSourceType::Instrument) {
            // I pill: audio = "pass input through to output"; instrument =
            // "this plugin receives live MIDI." See docs/LIVE_INPUT_AND_FOCUS.md.
            // Auto-flipped on focus change for instruments; always user-explicit
            // for audio (feedback protection).
            drawPill(inputMonitorBounds[i], "I", trackState->inputMonitoring, Theme::Color::pillInput,
                     isHoveredRow && hoveredPill == HoveredPill::Input);
        }

        y += rowH;
    }
}

void ProducePane::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    g.reduceClipRegion(area);

    if (!state) return;

    auto tracks = state->listTracks();
    int gridWidth = area.getWidth();
    double startBeat = scrollBeat;
    double endBeat = startBeat + gridWidth / pixelsPerBeat;

    // Track lane backgrounds — paintTrackRow owns row colour + selection.
    // Inset the left edge by 1px so fills don't overdraw the track-header
    // column's right border (drawn earlier in paintTrackHeaders at x=area.getX()).
    int laneY = area.getY();
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto* trkState = state->findTrack(tracks[i].id);
        if (!trkState) { laneY += trackRowHeight; continue; }
        int rowH = rowHeightFor(*trkState);
        auto laneBounds = juce::Rectangle<int>(area.getX() + 1, laneY,
                                                area.getWidth() - 1, rowH);
        paintTrackRow(g, laneBounds, trackRowVisuals(*trkState));
        laneY += rowH;
    }

    // Grid lines — bar lines darker, beat lines lighter.
    // Clip vertically to the track-content region so gridlines don't bleed
    // into the empty area below the last track.
    int gridLinesBottom = std::min(area.getBottom(),
                                   area.getY() + rowYFor(tracks.size()));
    int startBar = (int)(startBeat / beatsPerBar());
    int endBar = (int)(endBeat / beatsPerBar()) + 1;

    for (int bar = startBar; bar <= endBar; ++bar) {
        for (int b = 0; b < beatsPerBar(); ++b) {
            double beat = bar * beatsPerBar() + b;
            int x = beatToX(beat);
            if (x < area.getX() || x > area.getRight()) continue;

            g.setColour(b == 0 ? Theme::color(Theme::Color::border)
                                : Theme::color(Theme::Color::borderSubtle));
            g.drawLine((float)x, (float)area.getY(), (float)x, (float)gridLinesBottom, 0.5f);
        }
    }

    // Track row separators
    int y = area.getY();
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto* trkState = state->findTrack(tracks[i].id);
        y += trkState ? rowHeightFor(*trkState) : trackRowHeight;
        g.setColour(Theme::color(Theme::Color::borderSubtle));
        g.drawLine((float)area.getX(), (float)y, (float)area.getRight(), (float)y, 0.5f);
    }

    // Regions
    // Regions — cache hit rects for mouse interaction
    regionHitRects.clear();
    actionHitRects.clear();
    ghostEdgeRects.clear();
    if (arrangement) {
        int regionRowY = area.getY();
        for (size_t ti = 0; ti < tracks.size(); ++ti) {
            auto regions = arrangement->regionsForTrack(tracks[ti].id);
            std::sort(regions.begin(), regions.end(),
                      [](auto* a, auto* b) { return a->startBeat < b->startBeat; });

            // Resolve track type for region rendering
            auto* trkState = state ? state->findTrack(tracks[ti].id) : nullptr;
            bool isAudioTrk = trkState && trkState->sourceType == TrackSourceType::AudioInput;
            int rowH = trkState ? rowHeightFor(*trkState) : trackRowHeight;
            int rowY = regionRowY;
            regionRowY += rowH;
            // Regions use neutral bgSurface (no per-track color)

            for (auto* r : regions) {
                int rx = beatToX(r->startBeat);
                int rw = std::max(4, (int)(r->lengthBeats * pixelsPerBeat));
                auto regionBounds = juce::Rectangle<int>(rx, rowY + 2, rw, rowH - 4);

                // Compute full visual extent including ghost loops
                double visualEnd = r->startBeat + r->lengthBeats;
                if (r->looped) {
                    double le = r->loopEndBeat;
                    if (le <= 0.0) {
                        le = 1e9;
                        for (auto* other : regions)
                            if (other != r && !other->muted && other->startBeat >= visualEnd)
                                le = std::min(le, other->startBeat);
                        if (le > 1e8) le = r->startBeat + r->lengthBeats * 9;
                    }
                    visualEnd = le;
                }
                int visualEndX = beatToX(visualEnd);
                if (visualEndX < area.getX() || regionBounds.getX() > area.getRight())
                    continue;

                bool originalVisible = regionBounds.getRight() >= area.getX() && regionBounds.getX() <= area.getRight();
                if (originalVisible)
                    regionHitRects.push_back({ r->id, tracks[ti].id, regionBounds });

                auto rvis = trkState ? regionVisuals(*trkState, *r) : RegionVisuals{};
                auto fillCol = regionFillColour(rvis.audibility);
                if (originalVisible) {
                paintRegionShell(g, regionBounds, rvis);

                // Region name
                g.setColour(Theme::color(Theme::Color::textPrimary));
                g.setFont(Theme::font(Theme::fontSizeXs));
                g.drawText(juce::String(r->name), regionBounds.reduced(4, 0),
                           juce::Justification::centredLeft);

                // MIDI note preview — mini piano roll
                if (r->type == "midi") {
                    auto noteList = Arrangement::buildNoteList(*r);
                    if (!noteList.empty()) {
                        // Find pitch range for vertical scaling
                        int minNote = 127, maxNote = 0;
                        for (auto& n : noteList) {
                            minNote = std::min(minNote, n.noteNumber);
                            maxNote = std::max(maxNote, n.noteNumber);
                        }
                        int noteRange = std::max(1, maxNote - minNote);
                        // Pad range for visual breathing room
                        int pad = std::max(2, noteRange / 4);
                        int lo = std::max(0, minNote - pad);
                        int hi = std::min(127, maxNote + pad);
                        int span = std::max(1, hi - lo);

                        auto inner = regionBounds.reduced(1, 3);
                        constexpr float noteH = 2.0f;

                        for (auto& note : noteList) {
                            // Skip notes outside region bounds (trimmed away)
                            if (note.beatOffset < 0.0 || note.beatOffset >= r->lengthBeats) continue;
                            double dispOffset = note.beatOffset;
                            if (r->quantize > 0.0)
                                dispOffset = std::round(dispOffset / r->quantize) * r->quantize;
                            float nx = (float)rx + (float)(dispOffset * pixelsPerBeat);
                            float nw = std::max(1.5f, (float)(note.durationBeats * pixelsPerBeat));
                            float ny = inner.getBottom() - ((note.noteNumber - lo) + 0.5f) * ((float)inner.getHeight() / span);

                            // Velocity → brightness + alpha, on the instrument-track hue.
                            float velNorm = juce::jlimit(0.0f, 1.0f, note.velocity / 127.0f);
                            auto base = Theme::color(Theme::Color::typeInstrument);
                            auto noteCol = base.darker(0.55f)
                                               .interpolatedWith(base.brighter(0.45f), velNorm)
                                               .withAlpha(0.75f + velNorm * 0.2f);
                            g.setColour(noteCol);
                            g.fillRect(nx, ny - noteH * 0.5f, nw, std::max(1.0f, noteH));
                        }
                    }
                }

                // Audio waveform preview — tinted with the audio-track type color,
                // gradient-filled vertically (intense at top, dim at bottom) so
                // taller peaks visually pop a touch more than shorter ones.
                // Subtle by design — the y-axis amplitude is the real signal; this
                // is denormalized sugar.
                if (r->type == "audio") {
                    auto* take = r->activeTake();
                    if (take && !take->peakData.peaks.empty()) {
                        auto inner = regionBounds.reduced(1, 3);
                        float centreY = inner.getCentreY();
                        float halfH = inner.getHeight() * 0.5f;

                        auto base = Theme::color(Theme::Color::typeAudio);
                        auto intense = base.brighter(0.25f).withAlpha(0.95f);
                        auto dim     = base.darker(0.15f).withAlpha(0.8f);
                        juce::ColourGradient grad(intense,
                                                   (float)inner.getX(), (float)inner.getY(),
                                                   intense,
                                                   (float)inner.getX(), (float)inner.getBottom(),
                                                   false);
                        grad.addColour(0.5, dim);  // dim at the silent center, intense at peak excursions
                        g.setGradientFill(grad);

                        // Map peaks to pixels
                        int numPeaks = (int)take->peakData.peaks.size();
                        double beatsPerPeak = (take->peakData.samplesPerPeak / (double)take->sampleRate)
                                              * (take->recordTempo / 60.0);
                        for (int pi = 0; pi < numPeaks; ++pi) {
                            float px = (float)rx + (float)(pi * beatsPerPeak * pixelsPerBeat);
                            float pw = std::max(1.0f, (float)(beatsPerPeak * pixelsPerBeat));
                            if (px + pw < inner.getX() || px > inner.getRight()) continue;

                            auto [mn, mx] = take->peakData.peaks[pi];
                            // Nonlinear scaling: sqrt boosts quiet signals
                            float scaledMx = (mx >= 0) ? std::sqrt(mx) : -std::sqrt(-mx);
                            float scaledMn = (mn >= 0) ? std::sqrt(mn) : -std::sqrt(-mn);
                            float y1 = centreY - scaledMx * halfH;
                            float y2 = centreY - scaledMn * halfH;
                            float h = std::max(1.0f, y2 - y1);
                            g.fillRect(px, y1, std::max(1.0f, pw), h);
                        }
                    }
                }

                // (Action track spheres are painted outside the region loop)

                }  // end originalVisible

                // Ghost loop copies
                if (r->looped && r->type == "midi") {
                    double loopEnd = visualEnd;  // already computed above
                    int maxGhosts = (int)std::ceil((loopEnd - r->startBeat) / r->lengthBeats) - 1;

                    // Build note list once for ghost note previews
                    auto ghostNotes = Arrangement::buildNoteList(*r);
                    int minNote = 127, maxNote = 0;
                    for (auto& n : ghostNotes) {
                        minNote = std::min(minNote, n.noteNumber);
                        maxNote = std::max(maxNote, n.noteNumber);
                    }
                    int noteRange = std::max(1, maxNote - minNote);
                    int pad = std::max(2, noteRange / 4);
                    int lo = std::max(0, minNote - pad);
                    int hi = std::min(127, maxNote + pad);
                    int span = std::max(1, hi - lo);

                    for (int gi = 1; gi <= maxGhosts; ++gi) {
                        double ghostStart = r->startBeat + gi * r->lengthBeats;
                        if (ghostStart >= loopEnd) break;
                        double ghostLen = std::min(r->lengthBeats, loopEnd - ghostStart);
                        int gx = beatToX(ghostStart);
                        int gw = std::max(4, (int)(ghostLen * pixelsPerBeat));
                        auto ghostBounds = juce::Rectangle<int>(gx, rowY + 2, gw, rowH - 4);

                        if (ghostBounds.getRight() < area.getX() || ghostBounds.getX() > area.getRight())
                            continue;

                        // Ghost fill
                        g.setColour(fillCol.withAlpha(0.35f));
                        g.fillRoundedRectangle(ghostBounds.toFloat(), 5.0f);

                        // Dashed border
                        g.setColour(fillCol.darker(0.3f).withAlpha(0.5f));
                        float dashLen = 4.0f, gapLen = 3.0f;
                        for (float dx = 0; dx < ghostBounds.getWidth(); dx += dashLen + gapLen) {
                            float x0 = ghostBounds.getX() + dx;
                            float x1 = std::min(x0 + dashLen, (float)ghostBounds.getRight());
                            g.drawLine(x0, (float)ghostBounds.getY(), x1, (float)ghostBounds.getY(), 0.5f);
                            g.drawLine(x0, (float)ghostBounds.getBottom(), x1, (float)ghostBounds.getBottom(), 0.5f);
                        }

                        // Record right edge for resize interaction
                        ghostEdgeRects.push_back({ r->id, ghostBounds.getRight(),
                            ghostBounds.getY(), ghostBounds.getHeight() });

                        // Dimmed note preview — same hue as the originals, lower alpha overall
                        if (!ghostNotes.empty()) {
                            auto inner = ghostBounds.reduced(1, 3);
                            constexpr float noteH = 2.0f;
                            auto base = Theme::color(Theme::Color::typeInstrument);
                            for (auto& note : ghostNotes) {
                                if (note.beatOffset >= ghostLen) continue;
                                float nx = (float)gx + (float)(note.beatOffset * pixelsPerBeat);
                                float nw = std::max(1.5f, (float)(note.durationBeats * pixelsPerBeat));
                                float ny = inner.getBottom() - ((note.noteNumber - lo) + 0.5f) * ((float)inner.getHeight() / span);
                                float velNorm = juce::jlimit(0.0f, 1.0f, note.velocity / 127.0f);
                                auto noteCol = base.darker(0.4f)
                                                   .interpolatedWith(base, velNorm)
                                                   .withAlpha(0.18f + velNorm * 0.22f);
                                g.setColour(noteCol);
                                g.fillRect(nx, ny - noteH * 0.5f, nw, std::max(1.0f, noteH));
                            }
                        }
                    }
                }
            }
        }

        // Action track spheres (painted directly on track lane, no regions)
        for (size_t ti = 0; ti < tracks.size(); ++ti) {
            auto* trkState = state ? state->findTrack(tracks[ti].id) : nullptr;
            if (!trkState || trkState->sourceType != TrackSourceType::Action) continue;
            if (trkState->actionData.empty()) continue;

            int rowY = area.getY() + rowYFor(ti);
            int rowH = rowHeightFor(*trkState);
            float centreY = (float)(rowY + rowH / 2);
            float sphereR = std::min(5.0f, (float)rowH * 0.25f);

            bool isAct = true;
            auto sphereCol = Theme::color(Theme::Color::textSecondary);

            // Pre-compute duration in beats for each visible event
            struct ActionVis {
                const ActionEventData* ae;
                float durationBeats;
                int lane;  // 0 = center, 1 = up, -1 = down, 2 = up, -2 = down...
            };
            std::vector<ActionVis> visible;
            for (auto& ae : trkState->actionData) {
                if (ae.beat > endBeat) continue;
                float durBeats = 0;
                auto* actInfo = state->findActionById(ae.actionId);
                if (actInfo && actInfo->durationParamIndex >= 0) {
                    auto args = juce::JSON::parse(juce::String(ae.argsJson));
                    if (args.isArray() && actInfo->durationParamIndex < args.size()) {
                        float durSec = (float)args[actInfo->durationParamIndex];
                        if (durSec > 0) {
                            jassert(sequencer != nullptr);
                            durBeats = durSec * (float)(sequencer->getTempo() / 60.0);
                        }
                    }
                }
                double eventEnd = ae.beat + (double)durBeats;
                if (eventEnd < startBeat && durBeats > 0) continue;
                if (ae.beat < startBeat && durBeats == 0) continue;
                visible.push_back({ &ae, durBeats, 0 });
            }

            // Sort by beat for overlap detection
            std::sort(visible.begin(), visible.end(),
                      [](auto& a, auto& b) { return a.ae->beat < b.ae->beat; });

            // Assign lanes: for each event, find which lanes are occupied by
            // earlier events whose tails overlap this event's start
            for (int vi = 0; vi < (int)visible.size(); ++vi) {
                std::set<int> occupied;
                for (int vj = 0; vj < vi; ++vj) {
                    double tailEnd = visible[vj].ae->beat + (double)visible[vj].durationBeats;
                    // Overlap if tail extends past this event's start (or same beat)
                    bool overlaps = (visible[vj].durationBeats > 0 && tailEnd > visible[vi].ae->beat)
                                 || (visible[vj].ae->beat == visible[vi].ae->beat);
                    if (overlaps) occupied.insert(visible[vj].lane);
                }
                // Find first free lane: 0, 1, -1, 2, -2, ...
                int lane = 0;
                for (int try_ = 0; occupied.count(lane); ++try_) {
                    lane = ((try_ / 2) + 1) * ((try_ % 2 == 0) ? 1 : -1);
                }
                visible[vi].lane = lane;
            }

            // Draw
            float laneStep = sphereR * 2.0f + 2.0f;
            for (auto& v : visible) {
                float cx = (float)beatToX(v.ae->beat);
                float cy = centreY + v.lane * laneStep;

                // Duration bar
                if (v.durationBeats > 0) {
                    float barW = (float)(v.durationBeats * pixelsPerBeat);
                    g.setColour(sphereCol.withAlpha(0.18f));
                    g.fillRoundedRectangle(cx, cy - sphereR * 0.6f, barW, sphereR * 1.2f, 2.0f);
                    g.setColour(sphereCol.withAlpha(0.3f));
                    g.drawRoundedRectangle(cx, cy - sphereR * 0.6f, barW, sphereR * 1.2f, 2.0f, 0.5f);
                }

                // Hit rect
                int hitSize = (int)(sphereR * 2 + 4);
                actionHitRects.push_back({ v.ae->id, RegionId{}, tracks[ti].id,
                    juce::Rectangle<int>((int)(cx - hitSize/2), (int)(cy - hitSize/2), hitSize, hitSize) });

                // Sphere
                auto highlight = sphereCol.brighter(0.5f);
                juce::ColourGradient grad(highlight, cx - sphereR * 0.3f, cy - sphereR * 0.3f,
                                           sphereCol.darker(0.3f), cx + sphereR, cy + sphereR, true);
                g.setGradientFill(grad);
                g.fillEllipse(cx - sphereR, cy - sphereR, sphereR * 2, sphereR * 2);
                g.setColour(sphereCol.darker(0.5f).withAlpha(0.5f));
                g.drawEllipse(cx - sphereR, cy - sphereR, sphereR * 2, sphereR * 2, 0.5f);
            }
        }

        // Ghost region during drag (move or duplicate)
        if (draggingRegion) {
            auto* srcRegion = arrangement->findRegion(dragRegionId);
            if (srcRegion) {
                int gx = beatToX(dragCurrentBeat);
                int gw = std::max(4, (int)(srcRegion->lengthBeats * pixelsPerBeat));
                int drawY = area.getY() + rowYFor((size_t)std::max(0, dragCurrentTrackIdx));
                int targetH = trackRowHeight;
                if (dragCurrentTrackIdx >= 0 && dragCurrentTrackIdx < (int)tracks.size()) {
                    auto* tgt = state ? state->findTrack(tracks[dragCurrentTrackIdx].id) : nullptr;
                    if (tgt) targetH = rowHeightFor(*tgt);
                }
                auto ghostBounds = juce::Rectangle<int>(gx, drawY + 2, gw, targetH - 4);
                // Use target track's color for ghost
                uint32_t ghostTrackCol = Theme::Color::bgSurface;
                if (dragCurrentTrackIdx >= 0 && dragCurrentTrackIdx < (int)tracks.size()) {
                    // ghost uses bgSurface regardless of track
                }
                auto ghostCol = juce::Colour(ghostTrackCol);
                g.setColour(ghostCol.withAlpha(0.35f));
                g.fillRoundedRectangle(ghostBounds.toFloat(), 5.0f);
                g.setColour(Theme::color(Theme::Color::accent).withAlpha(0.5f));
                g.drawRoundedRectangle(ghostBounds.toFloat(), 5.0f, 1.5f);

                // "+" when option is held (duplicate mode)
                if (dragIsOption) {
                    g.setColour(Theme::color(Theme::Color::textOnColor).withAlpha(0.33f));
                    g.setFont(Theme::fontMono(Theme::fontSizeLcdMd));
                    g.drawText("+", ghostBounds, juce::Justification::centred);
                }
            }
        }
    }

    // Cycle guide lines in arrange area — only while dragging
    if (draggingCycle && sequencer) {
        double loopStart = sequencer->getLoopStart();
        double loopEnd = sequencer->getLoopEnd();
        if (loopEnd > loopStart) {
            auto lineCol = Theme::color(Theme::Color::transportCycle);
            int x1 = beatToX(loopStart);
            int x2 = beatToX(loopEnd);
            g.setColour(lineCol.withAlpha(0.5f));
            if (x1 >= trackHeaderWidth && x1 <= getWidth())
                g.drawLine((float)x1, (float)area.getY(), (float)x1, (float)area.getBottom(), 1.0f);
            if (x2 >= trackHeaderWidth && x2 <= getWidth())
                g.drawLine((float)x2, (float)area.getY(), (float)x2, (float)area.getBottom(), 1.0f);
            // Subtle fill
            int cx1 = std::max(x1, trackHeaderWidth);
            int cx2 = std::min(x2, getWidth());
            if (cx2 > cx1) {
                g.setColour(lineCol.withAlpha(0.06f));
                g.fillRect(cx1, area.getY(), cx2 - cx1, area.getHeight());
            }
        }
    }
}

void ProducePane::paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area) {
    if (!sequencer) return;

    // Interpolate playhead position for smooth rendering between timer ticks
    double beat = sequencer->getBeatPosition();
    // The position is already updated at 10Hz by the coordinator. At 30fps paint,
    // we get ~3 paints per update. The position appears smooth because the timer
    // and paint rates are close enough. For even smoother motion, we could
    // interpolate based on elapsed time and tempo, but this is good enough.
    int x = beatToX(beat);
    if (x < trackHeaderWidth || x > getWidth()) return;

    g.setColour(Theme::color(Theme::Color::playhead));
    g.drawLine((float)x, (float)area.getY(), (float)x, (float)area.getBottom(), 1.0f);

    // Small triangle at top
    juce::Path tri;
    tri.addTriangle((float)x - 4, (float)area.getY(),
                     (float)x + 4, (float)area.getY(),
                     (float)x, (float)(area.getY() + 6));
    g.fillPath(tri);
}

void ProducePane::resized() {
    // Position metronome slider at the right end of the transport bar
    int sliderW = 70;
    int labelW = 24;
    int y = 14;
    int h = 24;
    int x = getWidth() - sliderW - labelW - 8;
    metronomeLabel.setBounds(x, y, labelW, h);
    metronomeSlider.setBounds(x + labelW, y, sliderW, h);
}

int ProducePane::getTrackIndexAtY(int y) const {
    int gridTop = transportHeight + rulerHeight;
    if (y < gridTop || !state) return -1;
    auto tracks = state->listTracks();
    int cursor = gridTop;
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto* ts = state->findTrack(tracks[i].id);
        int h = ts ? rowHeightFor(*ts) : trackRowHeight;
        if (y < cursor + h) return (int)i;
        cursor += h;
    }
    return -1;
}

void ProducePane::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();

    // Start drag on track header area
    if (event.getPosition().getX() < trackHeaderWidth) {
        int idx = getTrackIndexAtY(event.getPosition().getY());
        if (idx >= 0) {
            dragTrackIndex = idx;
            dragTargetIndex = idx;
            dragStartY = event.getPosition().getY();
        }
        return;
    }

    // Check for region click (grid area, below ruler)
    if (event.getPosition().getY() > transportHeight + rulerHeight
        && event.getPosition().getX() > trackHeaderWidth) {

        // Ghost loop edge drag or right-click "Trim loops to here"
        for (auto& ge : ghostEdgeRects) {
            if (event.getPosition().getY() >= ge.y && event.getPosition().getY() < ge.y + ge.height) {
                if (std::abs(event.getPosition().getX() - ge.rightX) <= trimHandleWidth && !event.mods.isPopupMenu()) {
                    draggingLoopEnd = true;
                    loopEndRegionId = ge.regionId;
                    return;
                }
            }
        }

        // Right-click on ghost → "Trim loops to here"
        if (event.mods.isPopupMenu() && arrangement) {
            // Check if click is inside any ghost region
            double clickBeat = snapBeatToGrid(xToBeat(event.getPosition().getX()));
            int trkIdx = getTrackIndexAtY(event.getPosition().getY());
            auto allTracks = state ? state->listTracks() : std::vector<StateAPI::TrackInfo>{};
            if (trkIdx >= 0 && trkIdx < (int)allTracks.size()) {
                auto trackRegions = arrangement->regionsForTrack(allTracks[trkIdx].id);
                for (auto* r : trackRegions) {
                    if (!r->looped || r->type != "midi") continue;
                    double regionEnd = r->startBeat + r->lengthBeats;
                    if (clickBeat < regionEnd) continue;  // click is on the original, not a ghost
                    // Check if click is within the loop extent
                    double loopEnd = r->loopEndBeat;
                    if (loopEnd <= 0.0) {
                        loopEnd = 1e9;
                        for (auto* other : trackRegions)
                            if (other != r && !other->muted && other->startBeat >= regionEnd)
                                loopEnd = std::min(loopEnd, other->startBeat);
                        if (loopEnd > 1e8) loopEnd = r->startBeat + r->lengthBeats * 9;
                    }
                    if (clickBeat < loopEnd) {
                        auto regionId = r->id;
                        juce::PopupMenu menu;
                        menu.addItem(1, "Trim loops to here");
                        menu.addItem(2, "Convert loops to regions");
                        menu.addItem(3, "Unloop");
                        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                            juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
                            [this, regionId, clickBeat](int result) {
                                if (!arrangement) return;
                                auto* r = arrangement->findRegion(regionId);
                                if (!r) return;
                                if (result == 1) {
                                    r->loopEndBeat = clickBeat;
                                } else if (result == 2) {
                                    // Convert: find track, duplicate
                                    TrackId trackId;
                                    if (state) {
                                        for (auto& ti : state->listTracks()) {
                                            auto regs = arrangement->regionsForTrack(ti.id);
                                            for (auto* rr : regs)
                                                if (rr->id == regionId) { trackId = ti.id; break; }
                                            if (!trackId.empty()) break;
                                        }
                                    }
                                    if (!trackId.empty()) {
                                        double le = r->loopEndBeat > 0 ? r->loopEndBeat : clickBeat + r->lengthBeats;
                                        int reps = (int)std::ceil((le - r->startBeat) / r->lengthBeats) - 1;
                                        for (int gi = 1; gi <= reps; ++gi) {
                                            double gs = r->startBeat + gi * r->lengthBeats;
                                            if (gs >= le) break;
                                            arrangement->duplicateRegion(regionId, trackId, gs);
                                        }
                                        r->looped = false;
                                        r->loopEndBeat = 0.0;
                                    }
                                    if (onRegionsChanged) onRegionsChanged();
                                } else if (result == 3) {
                                    r->looped = false;
                                    r->loopEndBeat = 0.0;
                                }
                                repaint();
                            });
                        return;
                    }
                }
            }
        }

        // Hit test action event spheres — left-click to drag, right-click for menu
        for (auto& hit : actionHitRects) {
            if (hit.bounds.contains(event.getPosition())) {
                if (event.mods.isPopupMenu()) {
                    // Right-click: small menu with Delete + Replace.
                    auto trkId = hit.trackId;
                    auto evId = hit.eventId;
                    auto screenPos = event.getScreenPosition();
                    juce::PopupMenu menu;
                    menu.addItem(1, "Delete");
                    menu.addItem(2, "Replace action...");
                    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                        juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
                        [this, trkId, evId, screenPos](int result) {
                            if (result <= 0) return;
                            auto* t = state ? state->findTrack(TrackId{trkId}) : nullptr;
                            if (!t) return;
                            if (result == 1) {
                                t->actionData.erase(
                                    std::remove_if(t->actionData.begin(), t->actionData.end(),
                                        [&](auto& e) { return e.id == evId; }), t->actionData.end());
                                state->markDirty(); repaint(); return;
                            }
                            // Replace: open the picker; on accept, swap the existing event.
                            ActionPicker::launch(*state, screenPos, /*filter*/ {},
                                [this, trkId, evId](const ActionInfo& action, const juce::var& args) {
                                    auto* t2 = state ? state->findTrack(TrackId{trkId}) : nullptr;
                                    if (!t2) return;
                                    for (auto& ae : t2->actionData) {
                                        if (ae.id == evId) {
                                            ae.actionId = action.id;
                                            ae.argsJson = juce::JSON::toString(args, true).toStdString();
                                            break;
                                        }
                                    }
                                    state->markDirty(); repaint();
                                });
                        });
                } else {
                    // Left-click: begin drag
                    dragActionEventId = hit.eventId;
                    dragActionTrackId = hit.trackId;
                    draggingActionEvent = false;
                }
                return;
            }
        }

        // Hit test against cached region bounds
        for (auto& hit : regionHitRects) {
            if (hit.bounds.contains(event.getPosition())) {
                bool alreadySelected = selectedRegionIds.count(hit.regionId) > 0;

                // Check for trim handle (only on selected regions)
                if (alreadySelected) {
                    int localX = event.getPosition().getX() - hit.bounds.getX();
                    if (localX <= trimHandleWidth) {
                        trimEdge = TrimEdge::Left;
                        trimRegionId = hit.regionId;
                        auto* r = arrangement ? arrangement->findRegion(hit.regionId) : nullptr;
                        if (r) { trimOrigStartBeat = r->startBeat; trimOrigLengthBeats = r->lengthBeats; }
                        return;
                    }
                    if (localX >= hit.bounds.getWidth() - trimHandleWidth) {
                        trimEdge = TrimEdge::Right;
                        trimRegionId = hit.regionId;
                        auto* r = arrangement ? arrangement->findRegion(hit.regionId) : nullptr;
                        if (r) { trimOrigStartBeat = r->startBeat; trimOrigLengthBeats = r->lengthBeats; }
                        return;
                    }
                }

                if (event.mods.isCommandDown()) {
                    // Cmd+click: toggle region in/out of selection
                    if (alreadySelected)
                        selectedRegionIds.erase(hit.regionId);
                    else
                        selectedRegionIds.insert(hit.regionId);
                } else if (!alreadySelected) {
                    // Plain click on unselected: select only this one
                    selectedRegionIds.clear();
                    selectedRegionIds.insert(hit.regionId);
                }
                // If already selected and no modifier, keep selection (allows drag)

                dragRegionId = hit.regionId;
                draggingRegion = false;  // will start on mouseDrag
                dragIsOption = event.mods.isAltDown();
                dragStartBeat = xToBeat(event.getPosition().getX());
                dragStartTrackIdx = getTrackIndexAtY(event.getPosition().getY());
                dragCurrentBeat = dragStartBeat;
                dragCurrentTrackIdx = dragStartTrackIdx;
                repaint();
                return;
            }
        }

        // Click on empty area of an action track → open action picker.
        // Any button, no modifier needed: action tracks have no drag-select or
        // playhead semantics in the grid, so plain click is free for this.
        if (state) {
            int trkIdx = getTrackIndexAtY(event.getPosition().getY());
            if (trkIdx >= 0) {
                auto trkList = state->listTracks();
                if (trkIdx < (int)trkList.size()) {
                    auto* ts = state->findTrack(trkList[trkIdx].id);
                    if (ts && ts->sourceType == TrackSourceType::Action) {
                        double beat = snapBeatToGrid(xToBeat(event.getPosition().getX()));
                        showActionPicker(event.getScreenPosition(), trkList[trkIdx].id.str(), beat);
                        return;
                    }
                }
            }
        }

        // Clicked empty grid — deselect (playhead only moves via ruler)
        selectedRegionIds.clear();
        if (state) state->clearSelection();
        repaint();
        return;
    }

    // Click on ruler — plain click sets playhead, drag starts/adjusts cycle
    if (sequencer && event.getPosition().getY() >= transportHeight
        && event.getPosition().getY() < transportHeight + rulerHeight
        && event.getPosition().getX() > trackHeaderWidth) {
        double beat = snapBeatToGrid(xToBeat(event.getPosition().getX()));
        draggingCycle = false;
        draggingCycleEdge = CycleEdge::None;
        draggingCycleBody = false;

        double loopStart = sequencer->getLoopStart();
        double loopEnd = sequencer->getLoopEnd();
        bool hasCycle = loopEnd > loopStart;

        // Check if near an existing cycle edge
        if (hasCycle) {
            int startX = beatToX(loopStart);
            int endX = beatToX(loopEnd);
            int mx = event.getPosition().getX();
            if (std::abs(mx - startX) <= cycleEdgeThreshold) {
                draggingCycleEdge = CycleEdge::Start;
                dragStartY = event.getPosition().getY();
                return;
            }
            if (std::abs(mx - endX) <= cycleEdgeThreshold) {
                draggingCycleEdge = CycleEdge::End;
                dragStartY = event.getPosition().getY();
                return;
            }
            // Check if inside the cycle body — only a drag target when cycle
            // mode is actually on. With cycle off the region is visual-only;
            // a click in it should set the playhead like any ruler click.
            if (sequencer->isLoopEnabled() && beat >= loopStart && beat < loopEnd) {
                draggingCycleBody = true;
                cycleBodyDragOffset = beat - loopStart;
                dragStartY = event.getPosition().getY();
                return;
            }
        }

        cycleAnchorBeat = beat;
        dragStartY = event.getPosition().getY();
        return;
    }
}

void ProducePane::mouseDrag(const juce::MouseEvent& event) {
    // Ruler drag → cycle region (new or edge adjust)
    if (dragStartY >= transportHeight && dragStartY < transportHeight + rulerHeight
        && event.getPosition().getX() > trackHeaderWidth && sequencer) {
        double beat = snapBeatToGrid(xToBeat(event.getPosition().getX()));

        // Body drag — move entire cycle region
        if (draggingCycleBody) {
            double newStart = snapBeatToGrid(std::max(0.0, beat - cycleBodyDragOffset));
            double len = sequencer->getLoopEnd() - sequencer->getLoopStart();
            sequencer->setLoopRange(newStart, newStart + len);
            repaint();
            return;
        }

        // Edge drag — adjust existing cycle boundary
        if (draggingCycleEdge != CycleEdge::None) {
            double lo = sequencer->getLoopStart();
            double hi = sequencer->getLoopEnd();
            if (draggingCycleEdge == CycleEdge::Start)
                lo = std::min(beat, hi - 0.25);
            else
                hi = std::max(beat, lo + 0.25);
            sequencer->setLoopRange(lo, hi);
            repaint();
            return;
        }

        // New cycle drag
        if (!draggingCycle && event.getDistanceFromDragStart() > 3) {
            draggingCycle = true;
            sequencer->setLoopEnabled(true);
        }
        if (draggingCycle) {
            double lo = std::min(cycleAnchorBeat, beat);
            double hi = std::max(cycleAnchorBeat, beat);
            sequencer->setLoopRange(lo, hi);
            repaint();
        }
        return;
    }

    // Action event drag
    if (!dragActionEventId.empty() && state) {
        if (!draggingActionEvent && event.getDistanceFromDragStart() > 5)
            draggingActionEvent = true;
        if (draggingActionEvent) {
            double newBeat = snapBeatToGrid(xToBeat(event.getPosition().getX()));
            if (newBeat < 0) newBeat = 0;
            auto* t = state->findTrack(TrackId{dragActionTrackId});
            if (t) {
                for (auto& ae : t->actionData) {
                    if (ae.id == dragActionEventId) {
                        ae.beat = newBeat;
                        state->markDirty();
                        break;
                    }
                }
            }
            repaint();
        }
        return;
    }

    // Ghost loop end drag
    if (draggingLoopEnd && arrangement) {
        auto* region = arrangement->findRegion(loopEndRegionId);
        if (region) {
            double beat = snapBeatToGrid(xToBeat(event.getPosition().getX()));
            double minEnd = region->startBeat + region->lengthBeats;
            region->loopEndBeat = std::max(minEnd, beat);
            repaint();
        }
        return;
    }

    // Region trim drag
    if (trimEdge != TrimEdge::None && arrangement) {
        auto* region = arrangement->findRegion(trimRegionId);
        if (region) {
            double beatAtMouse = snapBeatToGrid(xToBeat(event.getPosition().getX()));
            if (trimEdge == TrimEdge::Left) {
                double maxStart = trimOrigStartBeat + trimOrigLengthBeats - 0.0625;
                double newStart = std::max(0.0, std::min(beatAtMouse, maxStart));
                double delta = newStart - region->startBeat;  // delta from current, not original
                region->startBeat = newStart;
                region->lengthBeats -= delta;
                // Adjust event offsets so notes stay at their absolute beat positions
                if (region->type == "midi" && delta != 0.0) {
                    auto* take = region->activeTake();
                    if (take) {
                        for (auto& ev : take->events)
                            ev.beatOffset -= delta;
                    }
                }
            } else {
                double minEnd = region->startBeat + 0.0625;
                double newEnd = std::max(minEnd, beatAtMouse);
                region->lengthBeats = newEnd - region->startBeat;
            }
            repaint();
        }
        return;
    }

    // Track header reorder drag
    if (dragTrackIndex >= 0) {
        int newTarget = getTrackIndexAtY(event.getPosition().getY());
        if (newTarget < 0) return;
        if (newTarget != dragTargetIndex) {
            dragTargetIndex = newTarget;
            repaint();
        }
        return;
    }

    // Region drag (starts after 5px threshold)
    if (!dragRegionId.empty() && sequencer) {
        if (!draggingRegion && event.getDistanceFromDragStart() > 5) {
            draggingRegion = true;
            // Capture the region's original beat for offset calculation
            auto* region = arrangement ? arrangement->findRegion(dragRegionId) : nullptr;
            if (region) {
                dragStartBeat = region->startBeat;
                dragCurrentBeat = region->startBeat;
            }
        }
        if (draggingRegion) {
            dragIsOption = event.mods.isAltDown();  // live update during drag
            double beatDelta = xToBeat(event.getPosition().getX()) - xToBeat(event.getMouseDownPosition().getX());
            double newBeat = snapBeatToGrid(std::max(0.0, dragStartBeat + beatDelta));
            dragCurrentBeat = newBeat;

            int trackIdx = getTrackIndexAtY(event.getPosition().getY());
            if (trackIdx >= 0 && state) {
                // Only allow drop on compatible track type
                auto tracks = state->listTracks();
                if (trackIdx < (int)tracks.size()) {
                    auto* targetTrack = state->findTrack(TrackId{tracks[trackIdx].id});
                    auto* region = arrangement ? arrangement->findRegion(dragRegionId) : nullptr;
                    if (targetTrack && region) {
                        bool compatible = false;
                        if (region->type == "midi" && targetTrack->sourceType == TrackSourceType::Instrument)
                            compatible = true;
                        else if (region->type == "audio" && targetTrack->sourceType == TrackSourceType::AudioInput)
                            compatible = true;
                        else if (region->type == "action" && targetTrack->sourceType == TrackSourceType::Action)
                            compatible = true;
                        if (compatible)
                            dragCurrentTrackIdx = trackIdx;
                    }
                }
            }

            repaint();
        }
    }
}

void ProducePane::handleTrackHeaderClick(int trackIdx, const juce::MouseEvent& event) {
    if (!state) return;
    auto tracks = state->listTracks();
    if (trackIdx < 0 || trackIdx >= (int)tracks.size()) return;
    auto& trackId = tracks[trackIdx].id;

    if (event.mods.isCommandDown()) {
        // Cmd+click: toggle this track in/out of selection
        state->selectTrack(trackId, true);
    } else if (event.mods.isShiftDown() && !selectionAnchorTrackId.empty()) {
        // Shift+click: range select from anchor to here
        int anchorIdx = -1;
        for (int i = 0; i < (int)tracks.size(); ++i) {
            if (tracks[i].id == selectionAnchorTrackId) { anchorIdx = i; break; }
        }
        if (anchorIdx >= 0) {
            int lo = std::min(anchorIdx, trackIdx);
            int hi = std::max(anchorIdx, trackIdx);
            // Clear existing, select entire range
            state->clearSelection();
            for (int i = lo; i <= hi; ++i)
                state->selectTrack(tracks[i].id, true);
        } else {
            state->selectTrack(trackId, false);
        }
    } else {
        // Plain click: select only this track and make it the focused one.
        // See docs/LIVE_INPUT_AND_FOCUS.md for the full policy — I-snap
        // here is gated off until phase 3 engine work lands.
        state->selectTrack(trackId, false);
        state->setFocusedTrackId(trackId);
        selectionAnchorTrackId = trackId;
    }

    // Also select all regions on selected tracks
    selectedRegionIds.clear();
    if (arrangement) {
        auto sel = state->selectedTrackIds();
        for (auto& sid : sel) {
            auto regs = arrangement->regionsForTrack(sid);
            for (auto* r : regs)
                selectedRegionIds.insert(r->id);
        }
    }
    repaint();
}

void ProducePane::mouseUp(const juce::MouseEvent& event) {
    // Complete ruler interaction (cycle drag, edge drag, body drag, or plain click)
    if (dragStartY >= transportHeight && dragStartY < transportHeight + rulerHeight) {
        if (draggingCycleBody) {
            // Body drag complete
        } else if (draggingCycleEdge != CycleEdge::None) {
            // Edge drag complete
        } else if (draggingCycle && sequencer) {
            double lo = sequencer->getLoopStart();
            double hi = sequencer->getLoopEnd();
            if (hi - lo < 0.25)
                sequencer->setLoopEnabled(false);
        } else if (sequencer) {
            // Plain click (no drag) — set playhead
            double beat = snapBeatToGrid(xToBeat(event.getPosition().getX()));
            if (beat >= 0.0) {
                sequencer->setBeatPosition(beat);
                ensurePlayheadVisible();
            }
        }
        draggingCycle = false;
        draggingCycleEdge = CycleEdge::None;
        draggingCycleBody = false;
        dragStartY = 0;
        repaint();
        return;
    }

    // Complete loop end drag
    if (draggingLoopEnd) {
        draggingLoopEnd = false;
        loopEndRegionId = {};
        repaint();
        return;
    }

    // Complete action event drag
    if (!dragActionEventId.empty()) {
        dragActionEventId = {};
        dragActionTrackId = {};
        draggingActionEvent = false;
        repaint();
        return;
    }

    // Complete trim
    if (trimEdge != TrimEdge::None) {
        trimEdge = TrimEdge::None;
        trimRegionId = {};
        if (onRegionsChanged) onRegionsChanged();
        repaint();
        return;
    }

    // Complete drag reorder (only if actually dragged)
    if (dragTrackIndex >= 0 && dragTargetIndex >= 0 && state) {
        if (dragTrackIndex != dragTargetIndex
            && event.getDistanceFromDragStart() > 5) {
            auto tracks = state->listTracks();
            if (dragTrackIndex < (int)tracks.size() && dragTargetIndex < (int)tracks.size()) {
                auto* srcTrack = state->findTrack(tracks[dragTrackIndex].id);
                auto* dstTrack = state->findTrack(tracks[dragTargetIndex].id);
                if (srcTrack && dstTrack) {
                    state->moveTrack(srcTrack->id, dstTrack->position);
                }
            }
            dragTrackIndex = -1;
            dragTargetIndex = -1;
            repaint();
            return;
        }

        // No significant drag — treat as click for selection/controls
        int idx = dragTrackIndex;
        dragTrackIndex = -1;
        dragTargetIndex = -1;

        auto tracks = state->listTracks();
        if (idx >= 0 && idx < (int)tracks.size()) {
            auto* trackState = state->findTrack(tracks[idx].id);

            // Mute "M"
            if (trackState
                && idx < (int)muteBounds.size()
                && !muteBounds[idx].isEmpty()
                && muteBounds[idx].expanded(4).contains(event.getPosition())) {
                state->setTrackMuted(tracks[idx].id, !trackState->muted);
                repaint();
                return;
            }

            // Solo "S"
            if (trackState
                && idx < (int)soloBounds.size()
                && !soloBounds[idx].isEmpty()
                && soloBounds[idx].expanded(4).contains(event.getPosition())) {
                state->setTrackSoloed(tracks[idx].id, !trackState->soloed);
                repaint();
                return;
            }

            // Arm "R"
            if (trackState
                && idx < (int)armBounds.size()
                && !armBounds[idx].isEmpty()
                && armBounds[idx].expanded(4).contains(event.getPosition())) {
                state->setTrackArmed(tracks[idx].id, !trackState->armed);
                repaint();
                return;
            }

            // Input monitoring "I" — audio input and instrument tracks.
            // Audio: toggles audio-in → output passthrough.
            // Instrument: toggles whether the plugin hears live MIDI.
            // See docs/LIVE_INPUT_AND_FOCUS.md.
            if (trackState
                && (trackState->sourceType == TrackSourceType::AudioInput
                    || trackState->sourceType == TrackSourceType::Instrument)
                && idx < (int)inputMonitorBounds.size()
                && !inputMonitorBounds[idx].isEmpty()
                && inputMonitorBounds[idx].expanded(4).contains(event.getPosition())) {
                state->setTrackInputMonitoring(tracks[idx].id, !trackState->inputMonitoring);
                repaint();
                return;
            }

            // Otherwise: track selection
            handleTrackHeaderClick(idx, event);
        }
        return;
    }
    dragTrackIndex = -1;
    dragTargetIndex = -1;

    // Complete region drag (move or duplicate)
    if (draggingRegion && arrangement && state) {
        auto tracks = state->listTracks();
        if (dragCurrentTrackIdx >= 0 && dragCurrentTrackIdx < (int)tracks.size()) {
            auto targetTrackId = tracks[dragCurrentTrackIdx].id;
            if (dragIsOption) {
                auto* newRegion = arrangement->duplicateRegion(dragRegionId, targetTrackId, dragCurrentBeat);
                if (newRegion) {
                    selectedRegionIds.clear();
                    selectedRegionIds.insert(newRegion->id);
                }
            } else {
                arrangement->moveRegion(dragRegionId, targetTrackId, dragCurrentBeat);
            }
            if (onRegionsChanged) onRegionsChanged();
        }
        draggingRegion = false;
        dragRegionId = {};
        repaint();
        return;
    }
    draggingRegion = false;
    dragRegionId = {};

    // Right-click on region — context menu
    if (event.mods.isPopupMenu() && !selectedRegionIds.empty() && arrangement) {
        // Check if click is on a selected region
        for (auto& hit : regionHitRects) {
            if (selectedRegionIds.count(hit.regionId) && hit.bounds.contains(event.getPosition())) {
                juce::PopupMenu menu;

                bool anyMuted = false, anyUnmuted = false;
                for (auto& rid : selectedRegionIds) {
                    auto* r = arrangement->findRegion(rid);
                    if (r && r->muted) anyMuted = true;
                    if (r && !r->muted) anyUnmuted = true;
                }
                if (anyUnmuted) menu.addItem(1, "Mute Region(s)");
                if (anyMuted) menu.addItem(3, "Unmute Region(s)");
                menu.addItem(2, "Delete Region(s)");
                menu.addSeparator();

                // Quantize submenu
                juce::PopupMenu quantMenu;
                quantMenu.addItem(100, "Off");
                quantMenu.addSeparator();
                quantMenu.addItem(101, "1/1 Note");
                quantMenu.addItem(102, "1/2 Note");
                quantMenu.addItem(103, "1/4 Note");
                quantMenu.addItem(104, "1/8 Note");
                quantMenu.addItem(105, "1/16 Note");
                quantMenu.addItem(106, "1/32 Note");
                quantMenu.addItem(107, "1/64 Note");
                quantMenu.addSeparator();
                quantMenu.addItem(110, "1/4 Triplet (1/6)");
                quantMenu.addItem(111, "1/8 Triplet (1/12)");
                quantMenu.addItem(112, "1/16 Triplet (1/24)");
                quantMenu.addItem(113, "1/32 Triplet (1/48)");
                menu.addSubMenu("Quantize", quantMenu);
                if (selectedRegionIds.size() >= 2)
                    menu.addItem(4, "Join Regions");

                // Loop options
                {
                    bool anyLooped = false, anyUnlooped = false;
                    for (auto& rid : selectedRegionIds) {
                        auto* rr = arrangement->findRegion(rid);
                        if (rr && rr->type == "midi") {
                            if (rr->looped) anyLooped = true;
                            else anyUnlooped = true;
                        }
                    }
                    menu.addSeparator();
                    if (anyUnlooped) menu.addItem(5, "Loop Region(s)");
                    if (anyLooped) menu.addItem(6, "Unloop Region(s)");
                    if (anyLooped) menu.addItem(7, "Convert Loops to Regions");
                }

                auto selIds = selectedRegionIds;
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                    juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
                    [this, selIds](int result) {
                        if (!arrangement) return;
                        if (result == 1) {
                            for (auto& rid : selIds) {
                                auto* r = arrangement->findRegion(rid);
                                if (r) r->muted = true;
                            }
                        } else if (result == 3) {
                            for (auto& rid : selIds) {
                                auto* r = arrangement->findRegion(rid);
                                if (r) r->muted = false;
                            }
                        } else if (result == 2) {
                            for (auto& rid : selIds)
                                arrangement->removeRegion(rid);
                            selectedRegionIds.clear();
                            if (onRegionsChanged) onRegionsChanged();
                        } else if (result == 4) {
                            joinSelectedRegions();
                        } else if (result == 5) {
                            for (auto& rid : selIds) {
                                auto* r = arrangement->findRegion(rid);
                                if (r && r->type == "midi") r->looped = true;
                            }
                        } else if (result == 6) {
                            for (auto& rid : selIds) {
                                auto* r = arrangement->findRegion(rid);
                                if (r) { r->looped = false; r->loopEndBeat = 0.0; }
                            }
                        } else if (result == 7) {
                            // Convert loops to regions
                            for (auto& rid : selIds) {
                                auto* r = arrangement->findRegion(rid);
                                if (!r || !r->looped) continue;
                                // Find owning track
                                TrackId trackId;
                                if (state) {
                                    for (auto& ti : state->listTracks()) {
                                        auto regs = arrangement->regionsForTrack(ti.id);
                                        for (auto* rr : regs)
                                            if (rr->id == rid) { trackId = ti.id; break; }
                                        if (!trackId.empty()) break;
                                    }
                                }
                                if (trackId.empty()) continue;
                                double loopEnd = r->loopEndBeat;
                                if (loopEnd <= 0.0) {
                                    loopEnd = 1e9;
                                    auto regs = arrangement->regionsForTrack(trackId);
                                    double rEnd = r->startBeat + r->lengthBeats;
                                    for (auto* other : regs) {
                                        if (other == r || other->muted) continue;
                                        if (other->startBeat >= rEnd)
                                            loopEnd = std::min(loopEnd, other->startBeat);
                                    }
                                    if (loopEnd > 1e8) loopEnd = r->startBeat + r->lengthBeats * 9;
                                }
                                int reps = (int)std::ceil((loopEnd - r->startBeat) / r->lengthBeats) - 1;
                                for (int gi = 1; gi <= reps; ++gi) {
                                    double gs = r->startBeat + gi * r->lengthBeats;
                                    if (gs >= loopEnd) break;
                                    arrangement->duplicateRegion(rid, trackId, gs);
                                }
                                r->looped = false;
                                r->loopEndBeat = 0.0;
                            }
                            if (onRegionsChanged) onRegionsChanged();
                        } else if (result >= 100 && result < 200) {
                            double grid = quantizeGridSize(result);
                            for (auto& rid : selIds) {
                                auto* r = arrangement->findRegion(rid);
                                if (r) r->quantize = grid;
                            }
                        }
                        repaint();
                    });
                return;
            }
        }
    }

    if (!sequencer) return;

    // BPM click — edit tempo
    if (bpmClickBounds.contains(event.getPosition()) && state) {
        auto* dlg = new juce::AlertWindow("Set Tempo", "", juce::MessageBoxIconType::NoIcon);
        dlg->addTextEditor("bpm", juce::String(state->getSongTempo(), 1), "BPM");
        dlg->addButton("OK", 1);
        dlg->addButton("Cancel", 0);
        auto* statePtr = state;
        auto* seqPtr = sequencer;
        dlg->enterModalState(true, juce::ModalCallbackFunction::create(
            [dlg, statePtr, seqPtr](int result) {
                if (result == 1) {
                    double bpm = dlg->getTextEditorContents("bpm").getDoubleValue();
                    if (bpm >= 20.0 && bpm <= 300.0) {
                        statePtr->setSongTempo(bpm);
                        seqPtr->setTempo(bpm);
                    }
                }
                delete dlg;
            }));
        return;
    }

    // Time sig click — edit time signature
    if (timeSigClickBounds.contains(event.getPosition()) && state) {
        auto [num, den] = state->getSongTimeSignature();
        auto* dlg = new juce::AlertWindow("Set Time Signature", "", juce::MessageBoxIconType::NoIcon);
        dlg->addTextEditor("timesig", juce::String(num) + "/" + juce::String(den), "Time Signature (e.g. 3/4)");
        dlg->addButton("OK", 1);
        dlg->addButton("Cancel", 0);
        auto* statePtr = state;
        auto* seqPtr = sequencer;
        dlg->enterModalState(true, juce::ModalCallbackFunction::create(
            [dlg, statePtr, seqPtr](int result) {
                if (result == 1) {
                    auto text = dlg->getTextEditorContents("timesig");
                    auto parts = juce::StringArray::fromTokens(text, "/", "");
                    if (parts.size() == 2) {
                        int n = parts[0].getIntValue();
                        int d = parts[1].getIntValue();
                        if (n >= 1 && n <= 32 && d >= 1 && d <= 32) {
                            statePtr->setSongTimeSignature(n, d);
                            seqPtr->setTimeSignature(n, d);
                        }
                    }
                }
                delete dlg;
            }));
        return;
    }

    // Transport buttons
    if (rewindButtonBounds.contains(event.getPosition())) {
        sequencer->setBeatPosition(0.0);
        ensurePlayheadVisible();
        repaint();
        return;
    }
    if (stopButtonBounds.contains(event.getPosition())) {
        if (sequencer->isPlaying()) sequencer->stop();
        else sequencer->setBeatPosition(0.0);  // second stop = rewind
        ensurePlayheadVisible();
        repaint();
        return;
    }
    if (playButtonBounds.contains(event.getPosition())) {
        sequencer->togglePlayStop();
        repaint();
        return;
    }
    if (recordButtonBounds.contains(event.getPosition())) {
        bool inRec = onIsRecordMode ? onIsRecordMode() : false;
        if (inRec) {
            if (onStopRecordMode) onStopRecordMode();
        } else {
            if (onStartRecordMode) onStartRecordMode();
        }
        repaint();
        return;
    }
    if (cycleButtonBounds.contains(event.getPosition())) {
        sequencer->setLoopEnabled(!sequencer->isLoopEnabled());
        repaint();
        return;
    }
}

void ProducePane::mouseDoubleClick(const juce::MouseEvent& event) {
    if (!state) { perfLog("[ProducePane] dblclick: no state\n"); return; }

    perfLog("[ProducePane] dblclick at (%d, %d) headerW=%d gridTop=%d\n",
            event.getPosition().getX(), event.getPosition().getY(),
            trackHeaderWidth, transportHeight + rulerHeight);

    // Double-click in grid area on action track → create action event
    if (event.getPosition().getX() >= trackHeaderWidth
        && event.getPosition().getY() > transportHeight + rulerHeight) {
        int trkIdx = getTrackIndexAtY(event.getPosition().getY());
        if (trkIdx >= 0) {
            auto tracks = state->listTracks();
            if (trkIdx < (int)tracks.size()) {
                auto* ts = state->findTrack(tracks[trkIdx].id);
                if (ts && ts->sourceType == TrackSourceType::Action) {
                    double beat = snapBeatToGrid(xToBeat(event.getPosition().getX()));
                    showActionPicker(event.getScreenPosition(), tracks[trkIdx].id.str(), beat);
                    return;
                }
            }
        }
    }

    if (event.getPosition().getX() >= trackHeaderWidth) return;

    int idx = getTrackIndexAtY(event.getPosition().getY());
    if (idx < 0) return;

    auto tracks = state->listTracks();
    if (idx >= (int)tracks.size()) return;

    int gridTop = transportHeight + rulerHeight;
    auto* ts = state->findTrack(tracks[idx].id);
    int rowH = ts ? rowHeightFor(*ts) : trackRowHeight;
    auto editBounds = juce::Rectangle<int>(28, gridTop + rowYFor((size_t)idx) + 4,
                                            trackHeaderWidth - 32, rowH - 8);
    auto trackId = tracks[idx].id;
    nameEditor.onCommit = [this, trackId](const juce::String& newName) {
        if (state) {
            state->renameTrack(trackId, newName.toStdString());
            repaint();
        }
    };
    nameEditor.show(*this, editBounds, juce::String(tracks[idx].name));
}

void ProducePane::mouseMove(const juce::MouseEvent& event) {
    auto mp = event.getPosition();
    auto newHover = HoveredTransport::None;
    if      (rewindButtonBounds.contains(mp)) newHover = HoveredTransport::Rewind;
    else if (stopButtonBounds.contains(mp))   newHover = HoveredTransport::Stop;
    else if (playButtonBounds.contains(mp))   newHover = HoveredTransport::Play;
    else if (recordButtonBounds.contains(mp)) newHover = HoveredTransport::Record;
    else if (cycleButtonBounds.contains(mp))  newHover = HoveredTransport::Cycle;
    if (newHover != hoveredTransport) {
        hoveredTransport = newHover;
        repaint();
    }

    // Cycle cursors in ruler
    if (sequencer && event.getPosition().getY() >= transportHeight
        && event.getPosition().getY() < transportHeight + rulerHeight
        && event.getPosition().getX() > trackHeaderWidth) {
        double ls = sequencer->getLoopStart(), le = sequencer->getLoopEnd();
        if (le > ls) {
            int mx = event.getPosition().getX();
            int startX = beatToX(ls);
            int endX = beatToX(le);
            if (std::abs(mx - startX) <= cycleEdgeThreshold || std::abs(mx - endX) <= cycleEdgeThreshold) {
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
            if (sequencer->isLoopEnabled()
                && mx > startX + cycleEdgeThreshold && mx < endX - cycleEdgeThreshold) {
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                return;
            }
        }
    }

    // Ghost loop edge resize cursor
    for (auto& ge : ghostEdgeRects) {
        if (std::abs(event.getPosition().getX() - ge.rightX) <= trimHandleWidth
            && event.getPosition().getY() >= ge.y && event.getPosition().getY() < ge.y + ge.height) {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }

    // Show resize cursor when hovering over trim handles of selected regions
    for (auto& hit : regionHitRects) {
        if (selectedRegionIds.count(hit.regionId) && hit.bounds.contains(event.getPosition())) {
            int localX = event.getPosition().getX() - hit.bounds.getX();
            if (localX <= trimHandleWidth || localX >= hit.bounds.getWidth() - trimHandleWidth) {
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);

    // Track pill hover (only matters on resting/off pills; paint reads these)
    int prevIdx = hoveredPillTrackIdx;
    HoveredPill prevPill = hoveredPill;
    hoveredPillTrackIdx = -1;
    hoveredPill = HoveredPill::None;

    auto pos = event.getPosition();
    auto checkPill = [&](const std::vector<juce::Rectangle<int>>& bounds, HoveredPill kind) {
        if (hoveredPill != HoveredPill::None) return;
        for (size_t i = 0; i < bounds.size(); ++i) {
            if (!bounds[i].isEmpty() && bounds[i].contains(pos)) {
                hoveredPillTrackIdx = (int)i;
                hoveredPill = kind;
                return;
            }
        }
    };
    checkPill(muteBounds,         HoveredPill::Mute);
    checkPill(soloBounds,         HoveredPill::Solo);
    checkPill(armBounds,          HoveredPill::Arm);
    checkPill(inputMonitorBounds, HoveredPill::Input);

    if (prevIdx != hoveredPillTrackIdx || prevPill != hoveredPill)
        repaint();
}

void ProducePane::mouseExit(const juce::MouseEvent& /*event*/) {
    bool dirty = false;
    if (hoveredPillTrackIdx != -1 || hoveredPill != HoveredPill::None) {
        hoveredPillTrackIdx = -1;
        hoveredPill = HoveredPill::None;
        dirty = true;
    }
    if (hoveredTransport != HoveredTransport::None) {
        hoveredTransport = HoveredTransport::None;
        dirty = true;
    }
    if (dirty) repaint();
}

void ProducePane::mouseWheelMove(const juce::MouseEvent& event,
                                   const juce::MouseWheelDetails& wheel) {
    if (event.mods.isCommandDown()) {
        // Zoom (pinch or Cmd+scroll)
        pixelsPerBeat = juce::jlimit(5.0, 100.0, pixelsPerBeat + wheel.deltaY * 10);
        saveZoomState();
    } else {
        // Horizontal scroll (two-finger swipe or deltaX)
        if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
            scrollBeat = std::max(0.0, scrollBeat - wheel.deltaX * 4);
        else
            scrollBeat = std::max(0.0, scrollBeat - wheel.deltaY * 4);
    }
    repaint();
}

bool ProducePane::keyPressed(const juce::KeyPress& key) {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused && dynamic_cast<juce::TextEditor*>(focused) != nullptr)
        return false;

    if (key == juce::KeyPress::spaceKey && sequencer) {
        sequencer->togglePlayStop();
        repaint();
        return true;
    }

    // Delete/Backspace: delete selected regions
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && !selectedRegionIds.empty() && arrangement) {
        for (auto& rid : selectedRegionIds)
            arrangement->removeRegion(rid);
        selectedRegionIds.clear();
        if (onRegionsChanged) onRegionsChanged();
        repaint();
        return true;
    }

    // m: toggle metronome, Shift+M increases vol, (future: decrease)
    if (key.getTextCharacter() == 'm' && sequencer) {
        sequencer->setMetronomeEnabled(!sequencer->isMetronomeEnabled());
        repaint();
        return true;
    }

    // Cmd+D: duplicate selected regions (place after originals)
    if (key.getTextCharacter() == 'd' && key.getModifiers().isCommandDown()
        && !selectedRegionIds.empty() && arrangement) {
        std::set<RegionId> newSelection;
        for (auto& rid : selectedRegionIds) {
            auto* region = arrangement->findRegion(rid);
            if (!region) continue;
            // Find owning track
            TrackId ownerTrackId;
            if (state) {
                auto tracks = state->listTracks();
                for (auto& t : tracks) {
                    auto regs = arrangement->regionsForTrack(t.id);
                    for (auto* r : regs) {
                        if (r->id == rid) { ownerTrackId = t.id; break; }
                    }
                    if (!ownerTrackId.empty()) break;
                }
            }
            if (!ownerTrackId.empty()) {
                auto* dup = arrangement->duplicateRegion(rid, ownerTrackId,
                                                          region->startBeat + region->lengthBeats);
                if (dup) newSelection.insert(dup->id);
            }
        }
        selectedRegionIds = newSelection;
        if (onRegionsChanged) onRegionsChanged();
        repaint();
        return true;
    }

    // r: start recording
    if (key.getTextCharacter() == 'r' && onStartRecordMode) {
        onStartRecordMode();
        repaint();
        return true;
    }

    // c: toggle cycle playback
    // l: toggle loop on selected regions (plain l only — Cmd+L is zoom-in)
    if (key.getTextCharacter() == 'l' && !key.getModifiers().isShiftDown()
        && !key.getModifiers().isCommandDown()
        && !selectedRegionIds.empty() && arrangement) {
        for (auto& rid : selectedRegionIds) {
            auto* r = arrangement->findRegion(rid);
            if (r && r->type == "midi") {
                r->looped = !r->looped;
                if (!r->looped) r->loopEndBeat = 0.0;
            }
        }
        repaint();
        return true;
    }

    if (key.getTextCharacter() == 'c' && sequencer) {
        sequencer->setLoopEnabled(!sequencer->isLoopEnabled());
        repaint();
        return true;
    }

    // u: set cycle locators from selected regions
    if (key.getTextCharacter() == 'u' && sequencer && arrangement && !selectedRegionIds.empty()) {
        double earliest = 1e9, latest = 0;
        for (auto& rid : selectedRegionIds) {
            auto* r = arrangement->findRegion(rid);
            if (!r) continue;
            earliest = std::min(earliest, r->startBeat);
            latest = std::max(latest, r->startBeat + r->lengthBeats);
        }
        if (latest > earliest) {
            sequencer->setLoopRange(earliest, latest);
            sequencer->setLoopEnabled(true);
        }
        repaint();
        return true;
    }

    // Return: snap playhead to beginning
    if (key == juce::KeyPress::returnKey && sequencer) {
        sequencer->setBeatPosition(0.0);
        ensurePlayheadVisible();
        repaint();
        return true;
    }

    // Cmd+h/l/j/k: zoom
    if (key.getModifiers().isCommandDown()) {
        auto c = key.getKeyCode();
        if (c == 'H' || c == 'h') {
            pixelsPerBeat = juce::jlimit(5.0, 200.0, pixelsPerBeat / 1.3);
            saveZoomState(); repaint(); return true;
        }
        if (c == 'L' || c == 'l') {
            pixelsPerBeat = juce::jlimit(5.0, 200.0, pixelsPerBeat * 1.3);
            saveZoomState(); repaint(); return true;
        }
        if (c == 'J' || c == 'j') {
            trackRowHeight = juce::jlimit(24, 120, (int)(trackRowHeight * 1.3));
            saveZoomState(); repaint(); return true;
        }
        if (c == 'K' || c == 'k') {
            trackRowHeight = juce::jlimit(24, 120, (int)(trackRowHeight / 1.3));
            saveZoomState(); repaint(); return true;
        }
    }

    // Shift+h/l: step playhead by one measure
    if (key.getModifiers().isShiftDown()
        && (key.getTextCharacter() == 'H' || key.getTextCharacter() == 'L') && sequencer) {
        int bpb = beatsPerBar();
        double beat = sequencer->getBeatPosition();
        if (key.getTextCharacter() == 'H') {
            double snapped = std::floor(beat / bpb) * bpb - bpb;
            sequencer->setBeatPosition(std::max(0.0, snapped));
        } else {
            double snapped = std::floor(beat / bpb) * bpb + bpb;
            sequencer->setBeatPosition(snapped);
        }
        ensurePlayheadVisible();
        repaint();
        return true;
    }

    // h/l: step playhead by one division (no Cmd modifier)
    if ((key.getTextCharacter() == 'h' || key.getTextCharacter() == 'l') && sequencer) {
        double divSize = 1.0 / sequencer->getTimeSignatureDenominator();
        double beat = sequencer->getBeatPosition();
        if (key.getTextCharacter() == 'h') {
            double snapped = std::floor(beat / divSize) * divSize - divSize;
            sequencer->setBeatPosition(std::max(0.0, snapped));
        } else {
            double snapped = std::floor(beat / divSize) * divSize + divSize;
            sequencer->setBeatPosition(snapped);
        }
        ensurePlayheadVisible();
        repaint();
        return true;
    }

    // Cmd+T: split selected region(s) at playhead
    if (key.getTextCharacter() == 't' && key.getModifiers().isCommandDown()
        && !selectedRegionIds.empty() && arrangement && sequencer) {
        double splitBeat = sequencer->getBeatPosition();
        std::set<RegionId> newSelection;
        for (auto& rid : selectedRegionIds) {
            auto* region = arrangement->findRegion(rid);
            if (!region) continue;
            // Only split if playhead is inside the region
            if (splitBeat > region->startBeat && splitBeat < region->startBeat + region->lengthBeats) {
                auto* right = arrangement->splitRegion(rid, splitBeat, true);  // splitNotes = keep crossing notes
                if (right) {
                    newSelection.insert(rid);
                    newSelection.insert(right->id);
                }
            } else {
                newSelection.insert(rid);
            }
        }
        selectedRegionIds = newSelection;
        if (onRegionsChanged) onRegionsChanged();
        repaint();
        return true;
    }

    return false;
}

void ProducePane::joinSelectedRegions() {
    if (!arrangement || !state || selectedRegionIds.size() < 2) return;

    // Group selected regions by track
    auto tracks = state->listTracks();
    for (auto& t : tracks) {
        // Collect selected regions on this track, sorted by start beat
        std::vector<RegionState*> trackRegs;
        for (auto* r : arrangement->regionsForTrack(t.id)) {
            if (selectedRegionIds.count(r->id))
                trackRegs.push_back(r);
        }
        if (trackRegs.size() < 2) continue;

        std::sort(trackRegs.begin(), trackRegs.end(),
                  [](auto* a, auto* b) { return a->startBeat < b->startBeat; });

        // Only join regions of the same type
        auto type = trackRegs[0]->type;
        bool allSameType = true;
        for (auto* r : trackRegs)
            if (r->type != type) { allSameType = false; break; }
        if (!allSameType) continue;

        // Build merged region: span from earliest start to latest end
        double minStart = trackRegs[0]->startBeat;
        double maxEnd = 0.0;
        for (auto* r : trackRegs)
            maxEnd = std::max(maxEnd, r->startBeat + r->lengthBeats);

        // Create the merged region as a copy of the first
        auto* merged = arrangement->addMidiRegion(t.id, minStart, maxEnd - minStart);
        if (!merged) continue;
        merged->type = type;
        merged->name = trackRegs[0]->name;

        // Merge events from all regions into the merged region's take
        auto* mergedTake = merged->activeTake();
        if (!mergedTake) continue;

        if (type == "midi") {
            for (auto* r : trackRegs) {
                auto* take = r->activeTake();
                if (!take) continue;
                double offsetDelta = r->startBeat - minStart;
                for (auto& ev : take->events) {
                    MidiEventState newEv = ev;
                    newEv.beatOffset += offsetDelta;
                    mergedTake->events.push_back(newEv);
                }
            }
            // Sort by beat offset
            std::sort(mergedTake->events.begin(), mergedTake->events.end(),
                      [](auto& a, auto& b) { return a.beatOffset < b.beatOffset; });
        }
        // For audio regions, join doesn't merge waveforms — just extend the span
        // (audio takes reference files, can't trivially merge)

        // Remove the source regions
        for (auto* r : trackRegs)
            arrangement->removeRegion(r->id);

        selectedRegionIds.clear();
        selectedRegionIds.insert(merged->id);
    }
    if (onRegionsChanged) onRegionsChanged();
    repaint();
}

double ProducePane::quantizeGridSize(int menuId) {
    switch (menuId) {
        case 100: return 0.0;       // Off
        case 101: return 4.0;       // 1/1
        case 102: return 2.0;       // 1/2
        case 103: return 1.0;       // 1/4
        case 104: return 0.5;       // 1/8
        case 105: return 0.25;      // 1/16
        case 106: return 0.125;     // 1/32
        case 107: return 0.0625;    // 1/64
        case 110: return 2.0 / 3.0; // 1/4 triplet
        case 111: return 1.0 / 3.0; // 1/8 triplet
        case 112: return 0.5 / 3.0; // 1/16 triplet
        case 113: return 0.25 / 3.0;// 1/32 triplet
        default:  return 0.0;
    }
}

