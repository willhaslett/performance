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

// Meter color: smooth gradient from green → amber → red
static juce::Colour meterColourForDb(float db) {
    if (db > 0.0f) {
        // 0 to +6: amber → bright red
        float t = juce::jlimit(0.0f, 1.0f, db / 6.0f);
        return juce::Colour(0xffccaa44).interpolatedWith(juce::Colour(0xffee3333), t);
    } else if (db > -12.0f) {
        // -12 to 0: green → amber
        float t = juce::jlimit(0.0f, 1.0f, (db + 12.0f) / 12.0f);
        return Theme::color(Theme::Color::midiActive).interpolatedWith(juce::Colour(0xffccaa44), t);
    }
    return Theme::color(Theme::Color::midiActive);
}

static void drawMeterBar(juce::Graphics& g, juce::Rectangle<int> area,
                          float level, float dbMin, float dbMax, float dbRange) {
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(area.toFloat(), 1.5f);

    if (level <= 0.0001f) return;

    float db = 20.0f * std::log10(level);
    db = std::max(db, dbMin);
    db = std::min(db, dbMax);
    float meterNorm = (db - dbMin) / dbRange;
    int meterHeight = (int)(area.getHeight() * meterNorm);
    auto fillArea = area.withTop(area.getBottom() - meterHeight);

    // Draw segmented gradient: split at 0dB and -12dB boundaries
    float zeroNorm = (0.0f - dbMin) / dbRange;
    float warmNorm = (-12.0f - dbMin) / dbRange;

    int zeroY = area.getBottom() - (int)(area.getHeight() * zeroNorm);
    int warmY = area.getBottom() - (int)(area.getHeight() * warmNorm);

    // Green zone (below -12dB)
    if (fillArea.getBottom() > warmY) {
        auto greenArea = fillArea.withTop(std::max(fillArea.getY(), warmY));
        g.setColour(Theme::color(Theme::Color::midiActive));
        g.fillRect(greenArea);
    }
    // Amber zone (-12 to 0dB)
    if (fillArea.getY() < warmY && fillArea.getBottom() > zeroY) {
        auto amberArea = fillArea.withTop(std::max(fillArea.getY(), zeroY))
                                  .withBottom(std::min(fillArea.getBottom(), warmY));
        g.setColour(juce::Colour(0xffccaa44));
        g.fillRect(amberArea);
    }
    // Red zone (above 0dB)
    if (fillArea.getY() < zeroY) {
        auto redArea = fillArea.withBottom(std::min(fillArea.getBottom(), zeroY));
        g.setColour(juce::Colour(0xffcc4444));
        g.fillRect(redArea);
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

    // dB tick marks — between fader and meter
    {
        g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        constexpr float ticks[] = { 6, 0, -6, -12, -24, -36, -48 };
        float tickX1 = (float)(faderArea.getRight() + 2);
        float tickX2 = (float)(faderArea.getRight() + gap - 2);
        float labelX = tickX1;
        float labelW = tickX2 - tickX1;

        for (float db : ticks) {
            float norm = (db - dbMin) / dbRange;
            float y = (float)faderArea.getBottom() - faderArea.getHeight() * norm;

            // Tick line
            g.setColour(Theme::color(Theme::Color::textDim).withAlpha(0.4f));
            g.drawLine(tickX1, y, tickX2, y, 0.5f);
        }

        // Just label 0dB — others would be too cramped
        float zeroNorm = (0.0f - dbMin) / dbRange;
        float zeroY = (float)faderArea.getBottom() - faderArea.getHeight() * zeroNorm;
        g.setColour(Theme::color(Theme::Color::textDim));
        g.drawText("0", (int)tickX1, (int)(zeroY - 4), (int)labelW + 2, 8,
                   juce::Justification::centred, false);
    }

    // Stereo meters: L on left, R on right
    auto meterLeft = meterArea.removeFromLeft(meterBarWidth);
    meterArea.removeFromLeft(meterGap);
    auto meterRight = meterArea.removeFromLeft(meterBarWidth);

    drawMeterBar(g, meterLeft, peakL, dbMin, dbMax, dbRange);
    drawMeterBar(g, meterRight, peakR, dbMin, dbMax, dbRange);
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
