#include "gui/TrackStrip.h"
#include "api/PerformanceAPI.h"
#include "engine/Log.h"

TrackStrip::TrackStrip(const juce::String& name, PerformanceAPI& api)
    : api(api), trackName(name) {}

void TrackStrip::setInstrumentName(const juce::String& name) {
    instrumentName = name;
    repaint();
}

void TrackStrip::setEffectNames(const std::vector<juce::String>& names) {
    effectNames = names;
    repaint();
}

void TrackStrip::setMidiEnabled(bool enabled) {
    midiEnabled = enabled;
    repaint();
}

void TrackStrip::setPeakLevel(float level) {
    if (std::abs(level - peakLevel) > 0.01f) {
        peakLevel = level;
        repaint();
    }
}

void TrackStrip::setGain(float gain) {
    if (draggingFader) return;  // don't override during drag
    if (std::abs(gain - gainValue) > 0.001f) {
        gainValue = gain;
        repaint();
    }
}

void TrackStrip::rebuildSlots() {
    slots.clear();
    int y = Theme::headerHeight + Theme::trackPadding;
    constexpr int faderMeterReserve = 28;  // fader + meter + margins
    int slotW = getWidth() - Theme::trackPadding * 2 - faderMeterReserve;
    int x = Theme::trackPadding;

    // Instrument slot (always present)
    slots.push_back({ juce::Rectangle<int>(x, y, slotW, Theme::slotHeight), 0 });
    y += Theme::slotHeight + Theme::slotPadding;

    // Gap between instrument and effects
    y += 4;

    // Effect slots
    for (int i = 0; i < (int)effectNames.size(); ++i) {
        slots.push_back({ juce::Rectangle<int>(x, y, slotW, Theme::slotHeight), i + 1 });
        y += Theme::slotHeight + Theme::slotPadding;
    }

    // Empty "add effect" slot (only if instrument is loaded)
    if (!instrumentName.isEmpty()) {
        slots.push_back({ juce::Rectangle<int>(x, y, slotW, Theme::slotHeight),
                          (int)effectNames.size() + 1 });
    }
}

void TrackStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    rebuildSlots();

    // Track background
    g.setColour(Theme::color(Theme::Color::bgTrack));
    g.fillRoundedRectangle(bounds.toFloat(), Theme::cornerRadius);

    // Border
    g.setColour(Theme::color(Theme::Color::border));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), Theme::cornerRadius, 1.0f);

    // Header
    headerBounds = bounds.removeFromTop(Theme::headerHeight);
    g.setColour(Theme::color(midiEnabled ? Theme::Color::bgHeader : Theme::Color::bgHeaderOff));
    g.fillRoundedRectangle(headerBounds.toFloat().reduced(1.0f, 1.0f),
                            Theme::cornerRadius);
    // Only round top corners — fill bottom of header rectangle
    g.fillRect(headerBounds.withTrimmedTop(Theme::headerHeight / 2).reduced(1, 0));

    g.setColour(Theme::color(Theme::Color::textWhite));
    g.setFont(Theme::font(Theme::fontSize));
    g.drawText(trackName, headerBounds.reduced(8, 0), juce::Justification::centredLeft);

    // MIDI indicator
    if (midiEnabled) {
        auto dot = headerBounds.removeFromRight(24).withSizeKeepingCentre(8, 8);
        g.setColour(Theme::color(Theme::Color::midiActive));
        g.fillEllipse(dot.toFloat());
    }

    // Instrument slot
    if (!slots.empty()) {
        auto& instSlot = slots[0];
        bool hovered = (hoveredSlot == 0);
        g.setColour(Theme::color(hovered ? Theme::Color::bgSlotHover : Theme::Color::bgSlot));
        g.fillRoundedRectangle(instSlot.bounds.toFloat(), Theme::cornerRadiusSm);

        g.setFont(Theme::font(Theme::fontSizeSm));
        if (instrumentName.isEmpty()) {
            g.setColour(Theme::color(Theme::Color::textDim));
            g.drawText("Instrument", instSlot.bounds.reduced(8, 0),
                       juce::Justification::centredLeft);
        } else {
            g.setColour(Theme::color(Theme::Color::instrument));
            g.drawText(instrumentName, instSlot.bounds.reduced(8, 0),
                       juce::Justification::centredLeft);
        }
    }

    // Effect slots
    for (size_t i = 1; i < slots.size(); ++i) {
        auto& slot = slots[i];
        bool hovered = (hoveredSlot == slot.index);
        g.setColour(Theme::color(hovered ? Theme::Color::bgSlotHover : Theme::Color::bgSlot));
        g.fillRoundedRectangle(slot.bounds.toFloat(), Theme::cornerRadiusSm);

        g.setFont(Theme::font(Theme::fontSizeSm));
        int effectIdx = slot.index - 1;
        if (effectIdx < (int)effectNames.size()) {
            g.setColour(Theme::color(Theme::Color::effect));
            g.drawText(effectNames[effectIdx], slot.bounds.reduced(8, 0),
                       juce::Justification::centredLeft);
        } else {
            g.setColour(Theme::color(Theme::Color::textDim));
            g.drawText("Effect", slot.bounds.reduced(8, 0),
                       juce::Justification::centredLeft);
        }
    }

    // Fader — vertical slider next to meter
    {
        constexpr float dbMin = -60.0f;
        constexpr float dbMax = 6.0f;
        constexpr float dbRange = dbMax - dbMin;
        constexpr int faderWidth = 8;
        constexpr int faderAreaRight = 6 + 4 + 4;  // meter width + margin + gap

        auto faderArea = getLocalBounds()
            .withTrimmedRight(faderAreaRight)
            .removeFromRight(faderWidth)
            .withTrimmedTop(Theme::headerHeight + 4)
            .withTrimmedBottom(4);

        // Groove
        auto groove = faderArea.withSizeKeepingCentre(2, faderArea.getHeight());
        g.setColour(Theme::color(Theme::Color::bgSlot));
        g.fillRoundedRectangle(groove.toFloat(), 1.0f);

        // Handle position from gain value (dB scale)
        float db = (gainValue > 0.0001f) ? 20.0f * std::log10(gainValue) : dbMin;
        db = std::max(db, dbMin);
        db = std::min(db, dbMax);
        float normalized = (db - dbMin) / dbRange;
        int handleY = faderArea.getBottom() - (int)(faderArea.getHeight() * normalized);

        // Handle
        auto handle = juce::Rectangle<int>(faderArea.getX() - 2, handleY - 4,
                                            faderWidth + 4, 8);
        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.fillRoundedRectangle(handle.toFloat(), 3.0f);

        // 0dB tick mark
        float zeroNorm = (0.0f - dbMin) / dbRange;
        int zeroY = faderArea.getBottom() - (int)(faderArea.getHeight() * zeroNorm);
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.drawLine((float)(faderArea.getX() - 3), (float)zeroY,
                   (float)(faderArea.getRight() + 3), (float)zeroY, 1.0f);
    }

    // VU meter — right edge, dB scale
    {
        constexpr int meterWidth = 6;
        constexpr int meterMargin = 4;
        constexpr float dbMin = -60.0f;
        constexpr float dbMax = 6.0f;
        constexpr float dbRange = dbMax - dbMin;

        auto meterArea = getLocalBounds()
            .removeFromRight(meterWidth + meterMargin)
            .removeFromRight(meterWidth)
            .withTrimmedTop(Theme::headerHeight + 4)
            .withTrimmedBottom(4);

        // Background
        g.setColour(Theme::color(Theme::Color::bgSlot));
        g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);

        // Level fill (dB scale)
        if (peakLevel > 0.0001f) {
            float db = 20.0f * std::log10(peakLevel);
            db = std::max(db, dbMin);
            db = std::min(db, dbMax);

            float normalized = (db - dbMin) / dbRange;  // 0..1
            int meterHeight = (int)(meterArea.getHeight() * normalized);
            auto fillArea = meterArea.withTop(meterArea.getBottom() - meterHeight);

            // Color by level: green → yellow → red
            if (db > 0.0f)
                g.setColour(juce::Colour(0xffcc4444));  // clipping
            else if (db > -6.0f)
                g.setColour(juce::Colour(0xffccaa44));  // hot
            else
                g.setColour(Theme::color(Theme::Color::midiActive));  // normal
            g.fillRoundedRectangle(fillArea.toFloat(), 2.0f);
        }
    }
}

static juce::Rectangle<int> getFaderArea(const juce::Rectangle<int>& bounds) {
    constexpr int faderWidth = 8;
    constexpr int faderAreaRight = 6 + 4 + 4;
    return bounds
        .withTrimmedRight(faderAreaRight)
        .removeFromRight(faderWidth + 6)  // wider hit area
        .withTrimmedTop(Theme::headerHeight + 4)
        .withTrimmedBottom(4);
}

void TrackStrip::mouseDown(const juce::MouseEvent& event) {
    auto faderHitArea = getFaderArea(getLocalBounds());
    if (faderHitArea.contains(event.getPosition())) {
        draggingFader = true;
        dragStartGain = gainValue;
        dragStartY = event.getPosition().getY();
    }
}

void TrackStrip::mouseDrag(const juce::MouseEvent& event) {
    if (!draggingFader) return;

    constexpr float dbMin = -60.0f;
    constexpr float dbMax = 6.0f;
    constexpr float dbRange = dbMax - dbMin;

    auto faderArea = getFaderArea(getLocalBounds());
    int deltaY = dragStartY - event.getPosition().getY();
    float dbDelta = (float)deltaY / (float)faderArea.getHeight() * dbRange;

    float startDb = (dragStartGain > 0.0001f) ? 20.0f * std::log10(dragStartGain) : dbMin;
    float newDb = std::max(dbMin, std::min(dbMax, startDb + dbDelta));
    float newGain = std::pow(10.0f, newDb / 20.0f);

    gainValue = newGain;
    api.setTrackGain(trackName, newGain);
    repaint();
}

void TrackStrip::mouseUp(const juce::MouseEvent& event) {
    if (draggingFader) {
        draggingFader = false;
        return;
    }

    for (auto& slot : slots) {
        if (!slot.bounds.contains(event.getPosition())) continue;

        bool isInstrument = (slot.index == 0);
        bool hasPlugin = isInstrument
            ? instrumentName.isNotEmpty()
            : (slot.index - 1 < (int)effectNames.size());

        if (event.mods.isPopupMenu()) {
            // Right click — context menu for populated slots
            if (hasPlugin)
                showSlotContextMenu(slot.index, isInstrument, event.getScreenPosition());
        } else {
            // Left click
            if (hasPlugin) {
                // Open plugin editor
                if (isInstrument)
                    api.openPluginEditor(trackName);
                else
                    api.openPluginEditor(trackName, effectNames[slot.index - 1]);
            } else {
                // Empty slot — pick a plugin
                showPluginPicker(slot.index, event.getScreenPosition());
            }
        }
        break;
    }
}

void TrackStrip::mouseMove(const juce::MouseEvent& event) {
    int newHovered = -1;
    for (auto& slot : slots) {
        if (slot.bounds.contains(event.getPosition())) {
            newHovered = slot.index;
            break;
        }
    }
    if (newHovered != hoveredSlot) {
        hoveredSlot = newHovered;
        repaint();
    }
}

void TrackStrip::mouseExit(const juce::MouseEvent&) {
    if (hoveredSlot != -1) {
        hoveredSlot = -1;
        repaint();
    }
}

void TrackStrip::showSlotContextMenu(int slotIndex, bool isInstrument,
                                       juce::Point<int> position) {
    juce::PopupMenu menu;
    menu.addItem(1, "No Plugin");
    menu.addSeparator();

    // Replace — submenu with plugin picker
    juce::PopupMenu replaceMenu;
    auto plugins = isInstrument ? api.listInstrumentPlugins() : api.listEffectPlugins();
    for (int p = 0; p < (int)plugins.size(); ++p) {
        auto& pluginName = plugins[p];
        juce::PopupMenu snapshotMenu;
        snapshotMenu.addItem(p * 1000 + 1, "Default");
        auto snapshots = api.listSnapshots(pluginName);
        if (!snapshots.empty()) {
            snapshotMenu.addSeparator();
            for (int s = 0; s < (int)snapshots.size(); ++s)
                snapshotMenu.addItem(p * 1000 + 100 + s, snapshots[s]);
        }
        replaceMenu.addSubMenu(pluginName, snapshotMenu);
    }
    menu.addSubMenu("Replace", replaceMenu);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, slotIndex, isInstrument, plugins](int result) {
            if (result == 0) return;

            if (result == 1) {
                if (isInstrument) {
                    api.removeInstrument(trackName);
                } else {
                    int effectIdx = slotIndex - 1;
                    if (effectIdx < (int)effectNames.size())
                        api.removeTrackEffect(trackName, effectNames[effectIdx]);
                }
                return;
            }

            // Replace — decode plugin + snapshot from result
            // Results from replace submenu start at 1000+
            int pluginIdx = result / 1000;
            int snapshotChoice = result % 1000;

            if (pluginIdx >= (int)plugins.size()) return;
            auto selectedPlugin = plugins[pluginIdx];

            juce::String snapshotName;
            if (snapshotChoice >= 100) {
                auto snapshots = api.listSnapshots(selectedPlugin);
                int snapIdx = snapshotChoice - 100;
                if (snapIdx < (int)snapshots.size())
                    snapshotName = snapshots[snapIdx];
            }

            if (isInstrument) {
                api.addInstrument(trackName, selectedPlugin, snapshotName);
            } else {
                api.addTrackEffect(trackName, selectedPlugin, selectedPlugin);
            }
        });
}

void TrackStrip::showPluginPicker(int slotIndex, juce::Point<int> position) {
    bool isInstrument = (slotIndex == 0);

    juce::PopupMenu menu;
    auto plugins = isInstrument ? api.listInstrumentPlugins() : api.listEffectPlugins();

    // Each plugin gets a submenu with snapshot options
    for (int p = 0; p < (int)plugins.size(); ++p) {
        auto& pluginName = plugins[p];

        juce::PopupMenu snapshotMenu;
        // Default — no snapshot
        snapshotMenu.addItem(p * 1000 + 1, "Default");

        // Saved snapshots
        auto snapshots = api.listSnapshots(pluginName);
        if (!snapshots.empty()) {
            snapshotMenu.addSeparator();
            for (int s = 0; s < (int)snapshots.size(); ++s)
                snapshotMenu.addItem(p * 1000 + 100 + s, snapshots[s]);
        }

        menu.addSubMenu(pluginName, snapshotMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, slotIndex, isInstrument, plugins](int result) {
            if (result == 0) return;

            int pluginIdx = result / 1000;
            int snapshotChoice = result % 1000;

            if (pluginIdx >= (int)plugins.size()) return;
            auto selectedPlugin = plugins[pluginIdx];

            juce::String snapshotName;
            if (snapshotChoice >= 100) {
                auto snapshots = api.listSnapshots(selectedPlugin);
                int snapIdx = snapshotChoice - 100;
                if (snapIdx < (int)snapshots.size())
                    snapshotName = snapshots[snapIdx];
            }

            if (isInstrument) {
                api.addInstrument(trackName, selectedPlugin, snapshotName);
            } else {
                api.addTrackEffect(trackName, selectedPlugin, selectedPlugin);
            }
        });
}
