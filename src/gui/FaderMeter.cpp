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
    peakLevel = level;
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

    // Meter background
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);

    // Meter fill
    if (peakLevel > 0.0001f) {
        float db = 20.0f * std::log10(peakLevel);
        db = std::max(db, dbMin);
        db = std::min(db, dbMax);
        float meterNorm = (db - dbMin) / dbRange;
        int meterHeight = (int)(meterArea.getHeight() * meterNorm);
        auto fillArea = meterArea.withTop(meterArea.getBottom() - meterHeight);

        if (db > 0.0f)
            g.setColour(juce::Colour(0xffcc4444));
        else if (db > -6.0f)
            g.setColour(juce::Colour(0xffccaa44));
        else
            g.setColour(Theme::color(Theme::Color::midiActive));
        g.fillRoundedRectangle(fillArea.toFloat(), 2.0f);
    }
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
