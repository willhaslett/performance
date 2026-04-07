#include "gui/PluginSlot.h"
#include "api/PerformanceAPI.h"
#include "engine/Log.h"

PluginSlot::PluginSlot(Type type, PerformanceAPI& api, const juce::String& id,
                       ParentKind parent)
    : slotType(type), parentKind(parent), api(api), parentId(id) {}

void PluginSlot::setPluginName(const juce::String& name) {
    if (pluginName != name) {
        pluginName = name;
        repaint();

        if (waitingForLoad && name.isNotEmpty()) {
            // Plugin assigned in registry — delay to let engine finish async loading
            startTimer(500);
        }
    }
}

void PluginSlot::timerCallback() {
    // openPluginEditor silently fails if the engine hasn't finished loading yet.
    // Retry a few times, then give up.
    stopTimer();
    waitingForLoad = false;
    api.openPluginEditor(parentId, slotType == Instrument ? "" : effectId);
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
            api.openPluginEditor(parentId, slotType == Instrument ? "" : effectId);
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
        auto presets = api.listPresets(pname);
        // Only show submenu if there are user presets beyond Default
        bool hasUserPresets = false;
        for (auto& pr : presets)
            if (pr != "Default") { hasUserPresets = true; break; }

        if (hasUserPresets) {
            juce::PopupMenu presetMenu;
            for (int s = 0; s < (int)presets.size(); ++s)
                presetMenu.addItem(p * 1000 + 100 + s, presets[s]);
            menu.addSubMenu(pname, presetMenu);
        } else {
            // No user presets — click plugin name directly
            menu.addItem(p * 1000 + 1, pname);
        }
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, plugins](int result) {
            if (result == 0) return;

            int pluginIdx = result / 1000;
            int presetChoice = result % 1000;
            if (pluginIdx >= (int)plugins.size()) return;

            auto selectedPlugin = plugins[pluginIdx];
            juce::String presetName;
            if (presetChoice >= 100) {
                auto presets = api.listPresets(selectedPlugin);
                int presetIdx = presetChoice - 100;
                if (presetIdx < (int)presets.size())
                    presetName = presets[presetIdx];
            }

            waitingForLoad = true;
            if (slotType == Instrument)
                api.addInstrument(parentId, selectedPlugin, presetName);
            else
                api.addEffect(parentId, selectedPlugin, selectedPlugin);

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
        auto presets = api.listPresets(pname);
        bool hasUserPresets = false;
        for (auto& pr : presets)
            if (pr != "Default") { hasUserPresets = true; break; }

        if (hasUserPresets) {
            juce::PopupMenu presetMenu;
            for (int s = 0; s < (int)presets.size(); ++s)
                presetMenu.addItem(p * 1000 + 100 + s, presets[s]);
            replaceMenu.addSubMenu(pname, presetMenu);
        } else {
            replaceMenu.addItem(p * 1000 + 1, pname);
        }
    }
    menu.addSubMenu("Replace", replaceMenu);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, plugins](int result) {
            if (result == 0) return;

            if (result == 1) {
                if (slotType == Instrument)
                    api.removeInstrument(parentId);
                else
                    api.removeEffect(parentId, effectId);
                if (onChanged) onChanged();
                return;
            }

            int pluginIdx = result / 1000;
            int presetChoice = result % 1000;
            if (pluginIdx >= (int)plugins.size()) return;

            auto selectedPlugin = plugins[pluginIdx];
            juce::String presetName;
            if (presetChoice >= 100) {
                auto presets = api.listPresets(selectedPlugin);
                int presetIdx = presetChoice - 100;
                if (presetIdx < (int)presets.size())
                    presetName = presets[presetIdx];
            }

            waitingForLoad = true;
            if (slotType == Instrument)
                api.addInstrument(parentId, selectedPlugin, presetName);
            else
                api.addEffect(parentId, selectedPlugin, selectedPlugin);

            if (onChanged) onChanged();
        });
}
