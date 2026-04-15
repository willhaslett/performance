#include "gui/SendsPanel.h"
#include "api/StateAPI.h"
#include <set>

SendsPanel::SendsPanel(const juce::String& trackId, StateAPI& state)
    : state(state), trackId(trackId) {}

void SendsPanel::setSends(const std::vector<SendInfo>& sends) {
    // Existing sends + one empty "add" row (only if busses exist)
    size_t expectedRows = sends.size() + (availableBusses.empty() ? 0 : 1);
    bool changed = (expectedRows != rows.size());
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
            rows.push_back({ s.busName, s.busId, s.gain, s.peakLevel });
        if (!availableBusses.empty())
            rows.push_back({ {}, {}, 1.0f, 0.0f });  // empty "add send" row (only if busses exist)

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

void SendsPanel::setAvailableBusses(const std::vector<BusOption>& busOptions) {
    bool wasEmpty = availableBusses.empty();
    availableBusses = busOptions;
    if (wasEmpty && !busOptions.empty() && rows.empty()) {
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
    return Theme::color(Theme::Color::activityOn);
}

void SendsPanel::paintKnob(juce::Graphics& g, juce::Rectangle<int> bounds,
                            float gain, float peakLevel) const {
    using pi = juce::MathConstants<float>;
    auto centre = bounds.getCentre().toFloat();
    float radius = (float)bounds.getWidth() * 0.5f - 1.0f;

    constexpr float minAngle = 3.6652f;
    constexpr float maxAngle = 8.9012f;
    constexpr float totalSweep = 5.2360f;

    float arcRadius = radius - 2.0f;

    g.setColour(Theme::color(Theme::Color::bgSurface));
    g.fillEllipse(bounds.toFloat().reduced(1.0f));

    if (peakLevel > 0.01f) {
        float innerRadius = arcRadius - 3.0f;
        g.setColour(juce::Colour(0xff1a6e1a));
        g.fillEllipse(centre.x - innerRadius, centre.y - innerRadius,
                      innerRadius * 2.0f, innerRadius * 2.0f);
    }

    juce::Path trackArc;
    trackArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                           0.0f, minAngle, maxAngle, true);
    g.setColour(Theme::color(Theme::Color::border));
    g.strokePath(trackArc, juce::PathStrokeType(2.0f));

    float db = (gain > 0.0001f) ? 20.0f * std::log10(gain) : -60.0f;
    db = std::max(-60.0f, std::min(6.0f, db));
    float normalized = (db + 60.0f) / 66.0f;

    if (normalized < 0.01f) {
        juce::Path hintArc;
        float hintSweep = totalSweep * 0.06f;
        hintArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                              0.0f, minAngle, minAngle + hintSweep, true);
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.strokePath(hintArc, juce::PathStrokeType(2.0f));
    } else {
        float valueAngle = minAngle + normalized * totalSweep;
        juce::Path valueArc;
        valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius,
                               0.0f, minAngle, valueAngle, true);
        g.setColour(Theme::color(Theme::Color::slotEffect));
        g.strokePath(valueArc, juce::PathStrokeType(2.0f));
    }
}

void SendsPanel::paint(juce::Graphics& g) {
    g.setFont(Theme::font(Theme::fontSizeXs));

    for (size_t i = 0; i < rows.size(); ++i) {
        auto pill = getPillBounds((int)i);

        g.setColour(Theme::color(Theme::Color::bgSurface));
        g.fillRoundedRectangle(pill.toFloat(), Theme::cornerRadiusSm);

        if (rows[i].busName.isNotEmpty()) {
            g.setColour(Theme::color(Theme::Color::slotEffect));
            g.drawText(rows[i].busName, pill.reduced(6, 0),
                       juce::Justification::centredLeft, true);
            paintKnob(g, getKnobBounds((int)i), rows[i].gain, rows[i].peakLevel);
        } else {
            g.setColour(Theme::color(Theme::Color::textDim));
            g.drawText("Send", pill.reduced(6, 0),
                       juce::Justification::centredLeft, true);
        }
    }
}

void SendsPanel::mouseDown(const juce::MouseEvent& event) {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].busName.isNotEmpty() &&
            getKnobBounds((int)i).expanded(4).contains(event.getPosition())) {
            draggingRow = (int)i;
            dragStartGain = rows[i].gain;
            dragStartY = event.getPosition().getY();
            return;
        }

        if (getPillBounds((int)i).contains(event.getPosition())) {
            if (event.mods.isPopupMenu() && rows[i].busName.isNotEmpty()) {
                // Right-click on existing send — delete
                auto sendId = rows[i].busId;
                auto tId = trackId;
                juce::PopupMenu menu;
                menu.addItem(1, "Remove Send");
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                    juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
                    [this, sendId, tId](int result) {
                        if (result == 1) {
                            // Find the send entity by bus ID and remove it
                            auto* song = state.currentSong();
                            if (song) {
                                for (auto& t : song->tracks) {
                                    if (t.id == tId.toStdString()) {
                                        for (auto& s : t.sends) {
                                            if (s.busId == sendId.toStdString()) {
                                                state.removeSend(s.id);
                                                break;
                                            }
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    });
            } else if (rows[i].busName.isEmpty()) {
                showBusPicker((int)i, event.getScreenPosition());
            }
            return;
        }
    }
}

void SendsPanel::mouseDrag(const juce::MouseEvent& event) {
    if (draggingRow < 0 || draggingRow >= (int)rows.size()) return;

    int deltaY = dragStartY - event.getPosition().getY();
    float dbDelta = (float)deltaY * 0.5f;

    float startDb = (dragStartGain > 0.0001f) ? 20.0f * std::log10(dragStartGain) : -60.0f;
    float newDb = std::max(-60.0f, std::min(6.0f, startDb + dbDelta));
    float newGain = std::pow(10.0f, newDb / 20.0f);

    rows[draggingRow].gain = newGain;
    state.setSendGainByBus(trackId.toStdString(), rows[draggingRow].busId.toStdString(), newGain);
    repaint();
}

void SendsPanel::mouseUp(const juce::MouseEvent&) {
    draggingRow = -1;
}

void SendsPanel::showBusPicker(int rowIndex, juce::Point<int> position) {
    juce::PopupMenu menu;
    for (int i = 0; i < (int)availableBusses.size(); ++i)
        menu.addItem(i + 1, availableBusses[i].name);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, rowIndex](int result) {
            if (result == 0 || result - 1 >= (int)availableBusses.size()) return;
            auto& bus = availableBusses[result - 1];
            state.addSend(trackId.toStdString(), bus.id.toStdString(), 1.0f);
        });
}
