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

void TrackStrip::rebuildSlots() {
    slots.clear();
    int y = Theme::headerHeight + Theme::trackPadding;
    int slotW = getWidth() - Theme::trackPadding * 2;
    int x = Theme::trackPadding;

    // Instrument slot (always present)
    slots.push_back({ juce::Rectangle<int>(x, y, slotW, Theme::slotHeight), 0 });
    y += Theme::slotHeight + Theme::slotPadding;

    // Separator line after instrument
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

    // Separator
    if (!instrumentName.isEmpty()) {
        int sepY = slots[0].bounds.getBottom() + Theme::slotPadding + 2;
        g.setColour(Theme::color(Theme::Color::border));
        g.drawLine((float)(Theme::trackPadding + 4), (float)sepY,
                   (float)(getWidth() - Theme::trackPadding - 4), (float)sepY, 1.0f);
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
}

void TrackStrip::mouseUp(const juce::MouseEvent& event) {
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
                // "No Plugin" — remove the plugin
                // TODO: implement removeInstrument / removeEffect on API
                perfLog("[TrackStrip] Remove plugin from slot %d (not yet implemented)\n", slotIndex);
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
