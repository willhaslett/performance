#include "gui/PluginSlot.h"
#include "api/PerformanceAPI.h"
#include "engine/Log.h"

PluginSlot::PluginSlot(Type type, PerformanceAPI& api, const juce::String& name,
                       ParentKind parent)
    : slotType(type), parentKind(parent), api(api), trackOrBusName(name) {}

void PluginSlot::setPluginName(const juce::String& name) {
    if (pluginName != name) {
        pluginName = name;
        repaint();
    }
}

void PluginSlot::paint(juce::Graphics& g) {
    g.setColour(Theme::color(hovered ? Theme::Color::bgSlotHover : Theme::Color::bgSlot));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), Theme::cornerRadiusSm);

    g.setFont(Theme::font(Theme::fontSizeSm));
    if (pluginName.isEmpty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.drawText(slotType == Instrument ? "Instrument" : "Effect",
                   getLocalBounds().reduced(8, 0), juce::Justification::centredLeft);
    } else {
        g.setColour(Theme::color(slotType == Instrument ? Theme::Color::instrument
                                                         : Theme::Color::effect));
        g.drawText(pluginName, getLocalBounds().reduced(8, 0),
                   juce::Justification::centredLeft);
    }
}

void PluginSlot::mouseUp(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu()) {
        if (hasPlugin())
            showContextMenu(event.getScreenPosition());
    } else {
        if (hasPlugin())
            api.openPluginEditor(trackOrBusName, slotType == Instrument ? "" : pluginName);
        else
            showPicker(event.getScreenPosition());
    }
}

void PluginSlot::mouseMove(const juce::MouseEvent&) {
    if (!hovered) { hovered = true; repaint(); }
}

void PluginSlot::mouseExit(const juce::MouseEvent&) {
    if (hovered) { hovered = false; repaint(); }
}

void PluginSlot::showPicker(juce::Point<int> position) {
    juce::PopupMenu menu;
    auto plugins = (slotType == Instrument) ? api.listInstrumentPlugins() : api.listEffectPlugins();

    for (int p = 0; p < (int)plugins.size(); ++p) {
        auto& pname = plugins[p];
        juce::PopupMenu snapshotMenu;
        snapshotMenu.addItem(p * 1000 + 1, "Default");
        auto snapshots = api.listSnapshots(pname);
        if (!snapshots.empty()) {
            snapshotMenu.addSeparator();
            for (int s = 0; s < (int)snapshots.size(); ++s)
                snapshotMenu.addItem(p * 1000 + 100 + s, snapshots[s]);
        }
        menu.addSubMenu(pname, snapshotMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, plugins](int result) {
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

            if (slotType == Instrument)
                api.addInstrument(trackOrBusName, selectedPlugin, snapshotName);
            else
                if (parentKind == OnTrack)
                    api.addTrackEffect(trackOrBusName, selectedPlugin, selectedPlugin);
                else
                    api.addBusEffect(trackOrBusName, selectedPlugin, selectedPlugin);

            if (onChanged) onChanged();
        });
}

void PluginSlot::showContextMenu(juce::Point<int> position) {
    juce::PopupMenu menu;
    menu.addItem(1, "No Plugin");
    menu.addSeparator();

    juce::PopupMenu replaceMenu;
    auto plugins = (slotType == Instrument) ? api.listInstrumentPlugins() : api.listEffectPlugins();
    for (int p = 0; p < (int)plugins.size(); ++p) {
        auto& pname = plugins[p];
        juce::PopupMenu snapshotMenu;
        snapshotMenu.addItem(p * 1000 + 1, "Default");
        auto snapshots = api.listSnapshots(pname);
        if (!snapshots.empty()) {
            snapshotMenu.addSeparator();
            for (int s = 0; s < (int)snapshots.size(); ++s)
                snapshotMenu.addItem(p * 1000 + 100 + s, snapshots[s]);
        }
        replaceMenu.addSubMenu(pname, snapshotMenu);
    }
    menu.addSubMenu("Replace", replaceMenu);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, plugins](int result) {
            if (result == 0) return;

            if (result == 1) {
                if (slotType == Instrument)
                    api.removeInstrument(trackOrBusName);
                else if (parentKind == OnTrack)
                    api.removeTrackEffect(trackOrBusName, pluginName);
                else
                    api.removeBusEffect(trackOrBusName, pluginName);
                if (onChanged) onChanged();
                return;
            }

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

            if (slotType == Instrument)
                api.addInstrument(trackOrBusName, selectedPlugin, snapshotName);
            else
                if (parentKind == OnTrack)
                    api.addTrackEffect(trackOrBusName, selectedPlugin, selectedPlugin);
                else
                    api.addBusEffect(trackOrBusName, selectedPlugin, selectedPlugin);

            if (onChanged) onChanged();
        });
}
