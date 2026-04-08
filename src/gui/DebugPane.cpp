#include "gui/DebugPane.h"
#include "api/PerformanceCoordinator.h"
#include "api/EngineAPI.h"

DebugPane::DebugPane(PerformanceCoordinator& coordinator, EngineAPI& engine)
    : coordinator(coordinator), engine(engine) {}

DebugPane::~DebugPane() {
    deactivate();
}

void DebugPane::activate() {
    if (active) return;
    active = true;
    activationTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    midiEvents.clear();
    midiScrollOffset = 0;

    coordinator.setGlobalMidiMonitor(
        [this](const std::string& deviceName, const std::string& description,
               const std::string& type, int channel, int number, int value) {
            double now = juce::Time::getMillisecondCounterHiRes() / 1000.0 - activationTime;
            midiEvents.push_back({ deviceName, description, type, channel, number, value, now });
            while ((int)midiEvents.size() > maxMidiEvents)
                midiEvents.pop_front();
            // Auto-scroll to bottom if already at bottom
            int totalRows = (int)midiEvents.size();
            int visibleRows = (getHeight() - headerHeight - audioPanelHeight) / midiRowHeight;
            if (midiScrollOffset >= totalRows - visibleRows - 2)
                midiScrollOffset = std::max(0, totalRows - visibleRows);
            repaint();
        });

    startTimerHz(20);  // 20fps for audio meters
}

void DebugPane::deactivate() {
    if (!active) return;
    active = false;
    stopTimer();
    coordinator.clearGlobalMidiMonitor();
}

void DebugPane::timerCallback() {
    // Update audio input levels
    inputLevels = engine.getInputPeakLevels();
    inputNames = engine.getInputChannelNames();
    repaint();
}

void DebugPane::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    g.fillAll(Theme::color(Theme::Color::bgPanel));

    if (!active) {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(14.0f));
        g.drawText("Debug pane inactive", bounds, juce::Justification::centred);
        return;
    }

    // --- Audio Input Meters (top section) ---
    auto audioArea = bounds.removeFromTop(audioPanelHeight);
    {
        g.setColour(Theme::color(Theme::Color::textWhite));
        g.setFont(Theme::font(12.0f));
        g.drawText("Audio Inputs", audioArea.removeFromTop(headerHeight).reduced(8, 0),
                    juce::Justification::centredLeft);

        auto metersArea = audioArea.reduced(8, 0);
        int numChannels = (int)inputLevels.size();
        if (numChannels == 0) {
            g.setColour(Theme::color(Theme::Color::textSecondary));
            g.setFont(Theme::font(11.0f));
            g.drawText("No audio inputs", metersArea, juce::Justification::centredLeft);
        } else {
            for (int i = 0; i < numChannels; ++i) {
                auto row = metersArea.removeFromTop(meterHeight + meterGap);
                auto labelArea = row.removeFromLeft(80);
                auto meterArea = row.reduced(0, 1);

                // Label
                g.setColour(Theme::color(Theme::Color::textSecondary));
                g.setFont(Theme::font(10.0f));
                juce::String label = (i < (int)inputNames.size() && inputNames[i].isNotEmpty())
                    ? inputNames[i] : ("Input " + juce::String(i + 1));
                g.drawText(label, labelArea, juce::Justification::centredLeft);

                // Meter background
                g.setColour(Theme::color(Theme::Color::bgSlot));
                g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);

                // Meter fill
                float level = (i < (int)inputLevels.size()) ? inputLevels[i] : 0.0f;
                float dbLevel = (level > 0.0001f) ? (20.0f * std::log10(level)) : -80.0f;
                float normalized = juce::jlimit(0.0f, 1.0f, (dbLevel + 60.0f) / 60.0f);
                if (normalized > 0.0f) {
                    auto fillArea = meterArea.toFloat();
                    fillArea.setWidth(fillArea.getWidth() * normalized);
                    auto colour = (dbLevel > -3.0f) ? juce::Colour(0xffff4444)
                                : (dbLevel > -12.0f) ? juce::Colour(0xffffaa00)
                                : juce::Colour(0xff44cc44);
                    g.setColour(colour);
                    g.fillRoundedRectangle(fillArea, 2.0f);
                }
            }
        }
    }

    // Separator
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)bounds.getY(), (float)getWidth(), (float)bounds.getY(), 1.0f);

    // --- MIDI Event Log (bottom section) ---
    auto midiArea = bounds;
    {
        auto headerArea = midiArea.removeFromTop(headerHeight);
        g.setColour(Theme::color(Theme::Color::textWhite));
        g.setFont(Theme::font(12.0f));
        g.drawText("MIDI Events", headerArea.reduced(8, 0), juce::Justification::centredLeft);

        // Column headers
        auto colHeaderArea = midiArea.removeFromTop(midiRowHeight);
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(10.0f));
        int x = colHeaderArea.getX() + 8;
        g.drawText("Time", x, colHeaderArea.getY(), 60, midiRowHeight, juce::Justification::centredLeft);
        g.drawText("Device", x + 60, colHeaderArea.getY(), 140, midiRowHeight, juce::Justification::centredLeft);
        g.drawText("Event", x + 200, colHeaderArea.getY(), 200, midiRowHeight, juce::Justification::centredLeft);
        g.drawText("Ch", x + 400, colHeaderArea.getY(), 30, midiRowHeight, juce::Justification::centredLeft);
        g.drawText("#", x + 430, colHeaderArea.getY(), 40, midiRowHeight, juce::Justification::centredLeft);
        g.drawText("Val", x + 470, colHeaderArea.getY(), 40, midiRowHeight, juce::Justification::centredLeft);

        g.setColour(Theme::color(Theme::Color::border));
        g.drawLine((float)midiArea.getX(), (float)midiArea.getY(),
                   (float)midiArea.getRight(), (float)midiArea.getY(), 1.0f);

        // Rows
        int visibleRows = midiArea.getHeight() / midiRowHeight;
        int startIdx = midiScrollOffset;
        int endIdx = std::min(startIdx + visibleRows, (int)midiEvents.size());

        g.setFont(Theme::font(10.0f));
        for (int i = startIdx; i < endIdx; ++i) {
            auto& ev = midiEvents[i];
            int rowY = midiArea.getY() + (i - startIdx) * midiRowHeight;

            // Alternating background
            if ((i - startIdx) % 2 == 1) {
                g.setColour(juce::Colour(0x08ffffff));
                g.fillRect(midiArea.getX(), rowY, midiArea.getWidth(), midiRowHeight);
            }

            // Event type color
            juce::Colour textCol = Theme::color(Theme::Color::textSecondary);
            if (ev.type == "NoteOn") textCol = juce::Colour(0xff44cc44);
            else if (ev.type == "NoteOff") textCol = juce::Colour(0xff888888);
            else if (ev.type == "CC") textCol = juce::Colour(0xff4499ff);
            else if (ev.type == "Pitch") textCol = juce::Colour(0xffffaa00);
            else if (ev.type == "Pressure") textCol = juce::Colour(0xffcc44cc);

            g.setColour(Theme::color(Theme::Color::textSecondary));
            char timeBuf[16];
            snprintf(timeBuf, sizeof(timeBuf), "%.2f", ev.timestamp);
            g.drawText(timeBuf, x, rowY, 60, midiRowHeight, juce::Justification::centredLeft);

            g.drawText(juce::String(ev.deviceName).substring(0, 20),
                       x + 60, rowY, 140, midiRowHeight, juce::Justification::centredLeft);

            g.setColour(textCol);
            g.drawText(juce::String(ev.description),
                       x + 200, rowY, 200, midiRowHeight, juce::Justification::centredLeft);

            g.setColour(Theme::color(Theme::Color::textSecondary));
            g.drawText(juce::String(ev.channel), x + 400, rowY, 30, midiRowHeight, juce::Justification::centredLeft);
            g.drawText(juce::String(ev.number), x + 430, rowY, 40, midiRowHeight, juce::Justification::centredLeft);
            g.drawText(juce::String(ev.value), x + 470, rowY, 40, midiRowHeight, juce::Justification::centredLeft);
        }
    }
}

void DebugPane::resized() {}

void DebugPane::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    int visibleRows = (getHeight() - headerHeight - audioPanelHeight - headerHeight - midiRowHeight) / midiRowHeight;
    int maxScroll = std::max(0, (int)midiEvents.size() - visibleRows);
    midiScrollOffset = juce::jlimit(0, maxScroll, midiScrollOffset - (int)(wheel.deltaY * 5));
    repaint();
}
