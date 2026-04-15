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
    // Fader travel area matches meter area exactly — handle extends beyond
    auto area = getLocalBounds().withTrimmedTop(topMargin).withTrimmedBottom(bottomMargin);
    return area.withTrimmedRight(meterWidth + labelWidth + gap + labelPad)
               .withSizeKeepingCentre(faderWidth, area.getHeight());
}

juce::Rectangle<int> FaderMeter::getMeterArea() const {
    auto area = getLocalBounds().withTrimmedTop(topMargin).withTrimmedBottom(bottomMargin);
    area.removeFromRight(labelWidth + labelPad);
    return area.removeFromRight(meterWidth);
}

// IEC-style piecewise linear curve
struct DbMapPoint { float db; float norm; };
static constexpr DbMapPoint dbMap[] = {
    { -60.0f, 0.00f }, { -48.0f, 0.08f }, { -36.0f, 0.18f },
    { -24.0f, 0.35f }, { -12.0f, 0.55f }, {  -6.0f, 0.70f },
    {   0.0f, 0.85f }, {   6.0f, 1.00f },
};
static constexpr int dbMapSize = sizeof(dbMap) / sizeof(dbMap[0]);

float FaderMeter::dbToNormalized(float db) {
    if (db <= dbMap[0].db) return dbMap[0].norm;
    if (db >= dbMap[dbMapSize - 1].db) return dbMap[dbMapSize - 1].norm;
    for (int i = 0; i < dbMapSize - 1; ++i) {
        if (db <= dbMap[i + 1].db) {
            float t = (db - dbMap[i].db) / (dbMap[i + 1].db - dbMap[i].db);
            return dbMap[i].norm + t * (dbMap[i + 1].norm - dbMap[i].norm);
        }
    }
    return 1.0f;
}

float FaderMeter::normalizedToDb(float norm) {
    if (norm <= dbMap[0].norm) return dbMap[0].db;
    if (norm >= dbMap[dbMapSize - 1].norm) return dbMap[dbMapSize - 1].db;
    for (int i = 0; i < dbMapSize - 1; ++i) {
        if (norm <= dbMap[i + 1].norm) {
            float t = (norm - dbMap[i].norm) / (dbMap[i + 1].norm - dbMap[i].norm);
            return dbMap[i].db + t * (dbMap[i + 1].db - dbMap[i].db);
        }
    }
    return 6.0f;
}

float FaderMeter::gainToNormalized(float gain) const {
    float db = (gain > 0.0001f) ? 20.0f * std::log10(gain) : dbMin;
    return dbToNormalized(db);
}

static void drawMeterBar(juce::Graphics& g, juce::Rectangle<int> area,
                          float level, float dbMin, float dbMax) {
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(area.toFloat(), 1.5f);

    if (level <= 0.0001f) return;

    float db = 20.0f * std::log10(level);
    db = std::max(db, dbMin);
    db = std::min(db, dbMax);
    float meterNorm = FaderMeter::dbToNormalized(db);
    int meterHeight = (int)(area.getHeight() * meterNorm);
    auto fillArea = area.withTop(area.getBottom() - meterHeight);

    int zeroY = area.getBottom() - (int)(area.getHeight() * FaderMeter::dbToNormalized(0.0f));
    int warmY = area.getBottom() - (int)(area.getHeight() * FaderMeter::dbToNormalized(-12.0f));

    // Green zone (below -12dB)
    if (fillArea.getBottom() > warmY) {
        auto zone = fillArea.withTop(std::max(fillArea.getY(), warmY));
        g.setColour(Theme::color(Theme::Color::activityOn));
        g.fillRect(zone);
    }
    // Amber zone (-12 to 0dB)
    if (fillArea.getY() < warmY && fillArea.getBottom() > zeroY) {
        auto zone = fillArea.withTop(std::max(fillArea.getY(), zeroY))
                             .withBottom(std::min(fillArea.getBottom(), warmY));
        g.setColour(juce::Colour(0xffccaa44));
        g.fillRect(zone);
    }
    // Red zone (above 0dB)
    if (fillArea.getY() < zeroY) {
        auto zone = fillArea.withBottom(std::min(fillArea.getBottom(), zeroY));
        g.setColour(juce::Colour(0xffcc4444));
        g.fillRect(zone);
    }
}

void FaderMeter::paint(juce::Graphics& g) {
    auto faderArea = getFaderArea();
    auto meterArea = getMeterArea();

    // --- 1. Grid lines first (painted under everything) ---
    {
        g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));

        struct Tick { float db; const char* label; };
        constexpr Tick ticks[] = {
            {   6, "+6" }, {   0, "0"  }, {  -6, "-6" },
            { -12, "12" }, { -24, "24" }, { -36, "36" }, { -48, "48" }
        };

        int labelX = getWidth() - labelWidth;
        float gridX1 = (float)(faderArea.getX() - 2);
        float gridX2 = (float)(labelX - labelPad);

        for (auto& tick : ticks) {
            float norm = dbToNormalized(tick.db);
            float y = (float)faderArea.getBottom() - faderArea.getHeight() * norm;

            g.setColour(Theme::color(Theme::Color::textDim).withAlpha(0.35f));
            g.drawLine(gridX1, y, gridX2, y, 0.5f);

            g.setColour(Theme::color(Theme::Color::textSecondary).withAlpha(tick.db == 0 ? 1.0f : 0.75f));
            g.drawText(tick.label, labelX, (int)(y - 6), labelWidth, 12,
                       juce::Justification::centredLeft, false);
        }
    }

    // --- 2. Fader groove + handle (paints over grid) ---
    auto groove = faderArea.withSizeKeepingCentre(2, faderArea.getHeight());
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(groove.toFloat(), 1.0f);

    float normalized = gainToNormalized(gainValue);
    // Handle center maps to full fader range (faderArea is already inset by handleHeight/2)
    int handleCenterY = faderArea.getBottom() - (int)(faderArea.getHeight() * normalized);
    int handleY = handleCenterY - handleHeight / 2;

    auto handle = juce::Rectangle<int>(faderArea.getX() - 2, handleY,
                                        faderWidth + 4, handleHeight);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.fillRoundedRectangle(handle.toFloat(), 3.0f);

    // --- 3. Stereo meters (paint over grid) ---
    auto meterLeft = meterArea.removeFromLeft(meterBarWidth);
    meterArea.removeFromLeft(meterGap);
    auto meterRight = meterArea.removeFromLeft(meterBarWidth);

    drawMeterBar(g, meterLeft, peakL, dbMin, dbMax);
    drawMeterBar(g, meterRight, peakR, dbMin, dbMax);
}

void FaderMeter::mouseDown(const juce::MouseEvent& event) {
    auto faderArea = getFaderArea();
    auto hitArea = faderArea.expanded(4, handleHeight);
    if (!hitArea.contains(event.getPosition())) return;

    // Check if clicking directly on the handle — if so, start relative drag
    float normalized = gainToNormalized(gainValue);
    int handleCenterY = faderArea.getBottom() - (int)(faderArea.getHeight() * normalized);
    bool onHandle = std::abs(event.getPosition().getY() - handleCenterY) <= handleHeight;

    if (onHandle) {
        dragging = true;
        dragStartGain = gainValue;
        dragStartY = event.getPosition().getY();
        if (onDragStart) onDragStart();
    } else {
        // Click-to-jump: set fader to clicked position
        dragging = true;
        float clickNorm = 1.0f - (float)(event.getPosition().getY() - faderArea.getY())
                                  / (float)faderArea.getHeight();
        clickNorm = juce::jlimit(0.0f, 1.0f, clickNorm);
        float newDb = normalizedToDb(clickNorm);
        float newGain = std::pow(10.0f, newDb / 20.0f);
        gainValue = newGain;
        dragStartGain = newGain;
        dragStartY = event.getPosition().getY();
        if (onGainChanged) onGainChanged(newGain);
        repaint();
    }
}

void FaderMeter::mouseDrag(const juce::MouseEvent& event) {
    if (!dragging) return;

    auto faderArea = getFaderArea();
    int deltaY = dragStartY - event.getPosition().getY();

    float startNorm = gainToNormalized(dragStartGain);
    float normDelta = (float)deltaY / (float)faderArea.getHeight();
    float newNorm = juce::jlimit(0.0f, 1.0f, startNorm + normDelta);
    float newDb = normalizedToDb(newNorm);
    float newGain = std::pow(10.0f, newDb / 20.0f);

    gainValue = newGain;
    if (onGainChanged) onGainChanged(newGain);
    repaint();
}

void FaderMeter::mouseUp(const juce::MouseEvent&) {
    if (dragging && onDragEnd) onDragEnd();
    dragging = false;
}
