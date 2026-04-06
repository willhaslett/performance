#include "gui/SendsPanel.h"
#include "api/PerformanceAPI.h"

SendsPanel::SendsPanel(const juce::String& trackName, PerformanceAPI& api)
    : api(api), trackName(trackName) {}

void SendsPanel::setSends(const std::vector<SendInfo>& sends) {
    // Rebuild rows: one per existing send + one empty "add send" row
    bool changed = (sends.size() + 1 != rows.size());
    if (!changed) {
        for (size_t i = 0; i < sends.size(); ++i) {
            if (rows[i].busName != sends[i].busName) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        rows.clear();
        for (auto& s : sends)
            rows.push_back({ s.busName, s.gain, s.peakLevel });
        rows.push_back({ {}, 1.0f, 0.0f });  // empty "add send" row

        if (auto* parent = getParentComponent())
            parent->resized();
    }

    // Update values on existing rows
    for (size_t i = 0; i < sends.size() && i < rows.size(); ++i) {
        rows[i].gain = sends[i].gain;
        rows[i].peakLevel = sends[i].peakLevel;
    }

    repaint();
}

void SendsPanel::setAvailableBusses(const std::vector<juce::String>& busNames) {
    bool wasEmpty = availableBusses.empty();
    availableBusses = busNames;
    if (wasEmpty && !busNames.empty() && rows.empty()) {
        std::vector<SendInfo> empty;
        setSends(empty);
    }
}

int SendsPanel::getDesiredHeight() const {
    if (rows.empty()) return 0;
    return (int)rows.size() * rowHeight + ((int)rows.size() - 1) * rowGap + bottomPadding;
}

juce::Rectangle<int> SendsPanel::getPillBounds(int row) const {
    int y = row * (rowHeight + rowGap);
    int knobSpace = knobSize + pillKnobGap;
    return { 0, y, getWidth() - knobSpace, rowHeight };
}

juce::Rectangle<int> SendsPanel::getKnobBounds(int row) const {
    int y = row * (rowHeight + rowGap);
    int knobX = getWidth() - knobSize;
    int knobY = y + (rowHeight - knobSize) / 2;
    return { knobX, knobY, knobSize, knobSize };
}

juce::Colour SendsPanel::vuColor(float peakLevel) {
    if (peakLevel < 0.0001f) return Theme::color(Theme::Color::textDim);
    float db = 20.0f * std::log10(peakLevel);
    if (db > 0.0f)  return juce::Colour(0xffcc4444);
    if (db > -6.0f) return juce::Colour(0xffccaa44);
    return Theme::color(Theme::Color::midiActive);
}

void SendsPanel::paintKnob(juce::Graphics& g, juce::Rectangle<int> bounds,
                            float gain, float peakLevel) const {
    using pi = juce::MathConstants<float>;
    auto centre = bounds.getCentre().toFloat();
    float radius = (float)bounds.getWidth() * 0.5f - 1.0f;

    // Arc range: 7:00 (min) clockwise through 9,12,3 to 5:00 (max) = 300° sweep
    // Confirmed via debug lines: 0=3:00, π/2=6:00, π=9:00, 3π/2=12:00
    // 7:00 = 2π/3, 5:00 = π/3, CW long way = π/3 + 2π
    // addCentredArc is offset π/2 from cos/sin (0 = 12:00, not 3:00)
    constexpr float minAngle = 3.6652f;   // 2π/3 + π/2 = 7:00
    constexpr float maxAngle = 8.9012f;   // π/3 + 2π + π/2 = 5:00 (long way CW)
    constexpr float totalSweep = 5.2360f; // 300°

    float arcRadius = radius - 2.0f;

    // Background circle
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillEllipse(bounds.toFloat().reduced(1.0f));

    // Signal glow fills the center of the knob — fixed brightness, on/off
    if (peakLevel > 0.01f) {
        float innerRadius = arcRadius - 3.0f;
        g.setColour(juce::Colour(0xff1a6e1a));  // muted/dark green — VU green with black mixed in
        g.fillEllipse(centre.x - innerRadius, centre.y - innerRadius,
                      innerRadius * 2.0f, innerRadius * 2.0f);
    }

    // Track arc (dim background showing full range)
    juce::Path trackArc;
    trackArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                           0.0f, minAngle, maxAngle, true);
    g.setColour(Theme::color(Theme::Color::border));
    g.strokePath(trackArc, juce::PathStrokeType(2.0f));

    // Compute normalized gain position
    float db = (gain > 0.0001f) ? 20.0f * std::log10(gain) : -60.0f;
    db = std::max(-60.0f, std::min(6.0f, db));
    float normalized = (db + 60.0f) / 66.0f;  // 0 = -60dB, 1 = +6dB

    if (normalized < 0.01f) {
        // At -infinity: hint arc at the start (7:00) to show it's interactive
        juce::Path hintArc;
        float hintSweep = totalSweep * 0.06f;  // ~18° hint
        hintArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                              0.0f, minAngle, minAngle + hintSweep, true);
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.strokePath(hintArc, juce::PathStrokeType(2.0f));
    } else {
        // Value arc from 7:00 clockwise to current position
        float valueAngle = minAngle + normalized * totalSweep;
        juce::Path valueArc;
        valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                               0.0f, minAngle, valueAngle, true);
        g.setColour(Theme::color(Theme::Color::effect));
        g.strokePath(valueArc, juce::PathStrokeType(2.0f));
    }
}

void SendsPanel::paint(juce::Graphics& g) {
    g.setFont(Theme::font(Theme::fontSizeXs));

    for (size_t i = 0; i < rows.size(); ++i) {
        auto pill = getPillBounds((int)i);

        // Pill background
        g.setColour(Theme::color(Theme::Color::bgSlot));
        g.fillRoundedRectangle(pill.toFloat(), Theme::cornerRadiusSm);

        // Pill text
        if (rows[i].busName.isNotEmpty()) {
            g.setColour(Theme::color(Theme::Color::effect));
            g.drawText(rows[i].busName, pill.reduced(6, 0),
                       juce::Justification::centredLeft, true);

            // Knob only for assigned sends
            paintKnob(g, getKnobBounds((int)i), rows[i].gain, rows[i].peakLevel);
        } else {
            g.setColour(Theme::color(Theme::Color::textDim));
            g.drawText("Send", pill.reduced(6, 0),
                       juce::Justification::centredLeft, true);
            // No knob for unassigned row
        }
    }
}

void SendsPanel::mouseDown(const juce::MouseEvent& event) {
    for (size_t i = 0; i < rows.size(); ++i) {
        // Click on knob — start drag (only assigned sends)
        if (rows[i].busName.isNotEmpty() &&
            getKnobBounds((int)i).expanded(4).contains(event.getPosition())) {
            draggingRow = (int)i;
            dragStartGain = rows[i].gain;
            dragStartY = event.getPosition().getY();
            return;
        }

        // Click on pill — show picker for empty slot
        if (getPillBounds((int)i).contains(event.getPosition())) {
            if (rows[i].busName.isEmpty())
                showBusPicker((int)i, event.getScreenPosition());
            return;
        }
    }
}

void SendsPanel::mouseDrag(const juce::MouseEvent& event) {
    if (draggingRow < 0 || draggingRow >= (int)rows.size()) return;

    int deltaY = dragStartY - event.getPosition().getY();
    float dbDelta = (float)deltaY * 0.5f;  // 0.5 dB per pixel

    float startDb = (dragStartGain > 0.0001f) ? 20.0f * std::log10(dragStartGain) : -60.0f;
    float newDb = std::max(-60.0f, std::min(6.0f, startDb + dbDelta));
    float newGain = std::pow(10.0f, newDb / 20.0f);

    rows[draggingRow].gain = newGain;
    api.setSendGain(trackName, rows[draggingRow].busName, newGain);
    repaint();
}

void SendsPanel::mouseUp(const juce::MouseEvent&) {
    draggingRow = -1;
}

void SendsPanel::showBusPicker(int rowIndex, juce::Point<int> position) {
    juce::PopupMenu menu;
    for (int i = 0; i < (int)availableBusses.size(); ++i)
        menu.addItem(i + 1, availableBusses[i]);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, rowIndex](int result) {
            if (result == 0 || result - 1 >= (int)availableBusses.size()) return;
            auto busName = availableBusses[result - 1];
            api.addSend(trackName, busName, 1.0f);
        });
}
