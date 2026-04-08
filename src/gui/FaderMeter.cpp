#include "gui/FaderMeter.h"

FaderMeter::FaderMeter() {}

void FaderMeter::setGain(float gain) {
    if (dragging) return;
    if (std::abs(gain - gainValue) > 0.001f) {
        gainValue = gain;
        repaint();
    }
}

void FaderMeter::setPeakLevel(float level) {
    peakL = level;
    peakR = level;
    repaint();
}

void FaderMeter::setPeakLevelStereo(float left, float right) {
    peakL = left;
    peakR = right;
    repaint();
}

juce::Rectangle<int> FaderMeter::getFaderArea() const {
    return getLocalBounds()
        .withTrimmedRight(meterWidth + gap)
        .withSizeKeepingCentre(faderWidth, getHeight());
}

juce::Rectangle<int> FaderMeter::getMeterArea() const {
    return getLocalBounds().removeFromRight(meterWidth);
}

float FaderMeter::gainToNormalized(float gain) const {
    float db = (gain > 0.0001f) ? 20.0f * std::log10(gain) : dbMin;
    db = std::max(db, dbMin);
    db = std::min(db, dbMax);
    return (db - dbMin) / dbRange;
}

static void drawMeterBar(juce::Graphics& g, juce::Rectangle<int> area, float level,
                          float dbMin, float dbMax, float dbRange) {
    // Background
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(area.toFloat(), 1.5f);

    if (level > 0.0001f) {
        float db = 20.0f * std::log10(level);
        db = std::max(db, dbMin);
        db = std::min(db, dbMax);
        float meterNorm = (db - dbMin) / dbRange;
        int meterHeight = (int)(area.getHeight() * meterNorm);
        auto fillArea = area.withTop(area.getBottom() - meterHeight);

        if (db > 0.0f)
            g.setColour(juce::Colour(0xffcc4444));
        else if (db > -6.0f)
            g.setColour(juce::Colour(0xffccaa44));
        else
            g.setColour(Theme::color(Theme::Color::midiActive));
        g.fillRoundedRectangle(fillArea.toFloat(), 1.5f);
    }
}

void FaderMeter::paint(juce::Graphics& g) {
    auto faderArea = getFaderArea();
    auto meterArea = getMeterArea();

    // Fader groove
    auto groove = faderArea.withSizeKeepingCentre(2, faderArea.getHeight());
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(groove.toFloat(), 1.0f);

    // Fader handle
    constexpr int handleHeight = 8;
    float normalized = gainToNormalized(gainValue);
    int travel = faderArea.getHeight() - handleHeight;
    int handleY = faderArea.getBottom() - handleHeight - (int)(travel * normalized);

    auto handle = juce::Rectangle<int>(faderArea.getX() - 2, handleY,
                                        faderWidth + 4, handleHeight);
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.fillRoundedRectangle(handle.toFloat(), 3.0f);

    // 0dB tick
    float zeroNorm = (0.0f - dbMin) / dbRange;
    int zeroY = faderArea.getBottom() - (int)(faderArea.getHeight() * zeroNorm);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.drawLine((float)(faderArea.getX() - 3), (float)zeroY,
               (float)(faderArea.getRight() + 3), (float)zeroY, 1.0f);

    // Stereo meters: L on left, R on right
    auto leftBar = meterArea.removeFromLeft(meterBarWidth);
    meterArea.removeFromLeft(meterGap);
    auto rightBar = meterArea.removeFromLeft(meterBarWidth);

    drawMeterBar(g, leftBar, peakL, dbMin, dbMax, dbRange);
    drawMeterBar(g, rightBar, peakR, dbMin, dbMax, dbRange);
}

void FaderMeter::mouseDown(const juce::MouseEvent& event) {
    auto hitArea = getFaderArea().expanded(4, 0);
    if (hitArea.contains(event.getPosition())) {
        dragging = true;
        dragStartGain = gainValue;
        dragStartY = event.getPosition().getY();
    }
}

void FaderMeter::mouseDrag(const juce::MouseEvent& event) {
    if (!dragging) return;

    auto faderArea = getFaderArea();
    int deltaY = dragStartY - event.getPosition().getY();
    float dbDelta = (float)deltaY / (float)faderArea.getHeight() * dbRange;

    float startDb = (dragStartGain > 0.0001f) ? 20.0f * std::log10(dragStartGain) : dbMin;
    float newDb = std::max(dbMin, std::min(dbMax, startDb + dbDelta));
    float newGain = std::pow(10.0f, newDb / 20.0f);

    gainValue = newGain;
    if (onGainChanged) onGainChanged(newGain);
    repaint();
}

void FaderMeter::mouseUp(const juce::MouseEvent&) {
    dragging = false;
}
