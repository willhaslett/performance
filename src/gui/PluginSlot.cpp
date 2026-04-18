#include "gui/PluginSlot.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "engine/Log.h"

PluginSlot::PluginSlot(Type type, StateAPI& state, EngineAPI& engine,
                       const juce::String& id, ParentKind parent)
    : slotType(type), parentKind(parent), state(state), engine(engine), parentId(id) {}

void PluginSlot::setPluginName(const juce::String& name) {
    if (pluginName != name) {
        pluginName = name;
        repaint();

        if (waitingForLoad && name.isNotEmpty()) {
            // Poll until the engine finishes async loading
            loadPollCount = 0;
            startTimer(100);
        }
    }
}

void PluginSlot::timerCallback() {
    // Poll LoadStatus until Loaded, then open editor
    if (++loadPollCount > 50) {  // 5 seconds max
        stopTimer();
        waitingForLoad = false;
        return;
    }

    auto* track = state.findTrack(parentId.toStdString());
    if (!track) return;

    bool loaded = (slotType == Instrument)
        ? track->instrumentLoadStatus == LoadStatus::Loaded
        : true;  // effects load synchronously

    if (loaded) {
        stopTimer();
        waitingForLoad = false;
        engine.openPluginEditor(parentId, slotType == Instrument ? "" : effectId);
    }
}

void PluginSlot::paint(juce::Graphics& g) {
    g.setColour(Theme::color(hovered ? Theme::Color::bgControlHover : Theme::Color::bgControl));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), Theme::cornerRadiusSm);

    g.setFont(Theme::font(Theme::fontSizeSm));
    if (pluginName.isEmpty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.drawText(slotType == Instrument ? "Instrument" : "Effect",
                   getLocalBounds().reduced(8, 0), juce::Justification::centredLeft);
    } else {
        g.setColour(Theme::color(slotType == Instrument ? Theme::Color::slotInstrument
                                                         : Theme::Color::slotEffect));
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
            engine.openPluginEditor(parentId, slotType == Instrument ? "" : effectId);
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

void PluginSlot::buildPluginMenu(juce::PopupMenu& menu,
                                  const std::vector<juce::String>& plugins,
                                  bool isInstrument) {
    if (isInstrument) {
        for (int p = 0; p < (int)plugins.size(); ++p) {
            auto& pname = plugins[p];
            auto presets = engine.listPresets(pname);
            bool hasUserPresets = false;
            for (auto& pr : presets)
                if (pr != "Default") { hasUserPresets = true; break; }

            if (hasUserPresets) {
                juce::PopupMenu presetMenu;
                for (int s = 0; s < (int)presets.size(); ++s)
                    presetMenu.addItem(p * 1000 + 100 + s, presets[s]);
                menu.addSubMenu(pname, presetMenu);
            } else {
                menu.addItem(p * 1000 + 1, pname);
            }
        }
    } else {
        std::map<juce::String, std::vector<int>> byManufacturer;
        for (int p = 0; p < (int)plugins.size(); ++p) {
            auto* info = state.findPluginByName(plugins[p].toStdString());
            auto mfr = info ? juce::String(info->manufacturer) : juce::String("Other");
            if (mfr.isEmpty()) mfr = "Other";
            byManufacturer[mfr].push_back(p);
        }

        for (auto& [mfr, indices] : byManufacturer) {
            juce::PopupMenu mfrMenu;
            for (int p : indices) {
                auto& pname = plugins[p];
                auto presets = engine.listPresets(pname);
                bool hasUserPresets = false;
                for (auto& pr : presets)
                    if (pr != "Default") { hasUserPresets = true; break; }

                if (hasUserPresets) {
                    juce::PopupMenu presetMenu;
                    for (int s = 0; s < (int)presets.size(); ++s)
                        presetMenu.addItem(p * 1000 + 100 + s, presets[s]);
                    mfrMenu.addSubMenu(pname, presetMenu);
                } else {
                    mfrMenu.addItem(p * 1000 + 1, pname);
                }
            }
            menu.addSubMenu(mfr, mfrMenu);
        }
    }
}

void PluginSlot::showPicker(juce::Point<int> position) {
    juce::PopupMenu menu;
    bool isInstrument = (slotType == Instrument);
    auto plugins = isInstrument ? engine.listInstrumentPlugins() : engine.listEffectPlugins();
    buildPluginMenu(menu, plugins, isInstrument);

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
                auto presets = engine.listPresets(selectedPlugin);
                int presetIdx = presetChoice - 100;
                if (presetIdx < (int)presets.size())
                    presetName = presets[presetIdx];
            }

            waitingForLoad = true;
            if (slotType == Instrument) {
                // Resolve plugin name to ID, then set on track
                auto pluginInfo = state.findPluginByName(selectedPlugin.toStdString());
                if (pluginInfo) {
                    PresetId presetId;
                    if (presetName.isNotEmpty()) {
                        auto preset = state.findPreset(pluginInfo->id, presetName.toStdString());
                        if (preset) presetId = preset->id;
                    }
                    state.setTrackPlugin(parentId.toStdString(), pluginInfo->id, presetId);
                }
            } else {
                auto pluginInfo = state.findPluginByName(selectedPlugin.toStdString());
                if (pluginInfo)
                    state.addEffect(parentId.toStdString(), selectedPlugin.toStdString(), pluginInfo->id);
            }

            if (onChanged) onChanged();
        });
}

void PluginSlot::showContextMenu(juce::Point<int> position) {
    juce::PopupMenu menu;
    menu.addItem(1, "No Plugin");
    menu.addSeparator();

    bool isInstrument = (slotType == Instrument);
    auto plugins = isInstrument ? engine.listInstrumentPlugins() : engine.listEffectPlugins();
    juce::PopupMenu replaceMenu;
    buildPluginMenu(replaceMenu, plugins, isInstrument);
    menu.addSubMenu("Replace", replaceMenu);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, plugins](int result) {
            if (result == 0) return;

            if (result == 1) {
                if (slotType == Instrument)
                    state.clearTrackPlugin(parentId.toStdString());
                else
                    state.removeEffect(effectId.toStdString());
                if (onChanged) onChanged();
                return;
            }

            int pluginIdx = result / 1000;
            int presetChoice = result % 1000;
            if (pluginIdx >= (int)plugins.size()) return;

            auto selectedPlugin = plugins[pluginIdx];
            juce::String presetName;
            if (presetChoice >= 100) {
                auto presets = engine.listPresets(selectedPlugin);
                int presetIdx = presetChoice - 100;
                if (presetIdx < (int)presets.size())
                    presetName = presets[presetIdx];
            }

            waitingForLoad = true;
            if (slotType == Instrument) {
                auto pluginInfo = state.findPluginByName(selectedPlugin.toStdString());
                if (pluginInfo) {
                    PresetId presetId;
                    if (presetName.isNotEmpty()) {
                        auto preset = state.findPreset(pluginInfo->id, presetName.toStdString());
                        if (preset) presetId = preset->id;
                    }
                    state.setTrackPlugin(parentId.toStdString(), pluginInfo->id, presetId);
                }
            } else {
                // Replace: remove old effect, then add new one in its place
                if (!effectId.isEmpty())
                    state.removeEffect(effectId.toStdString());
                auto pluginInfo = state.findPluginByName(selectedPlugin.toStdString());
                if (pluginInfo)
                    state.addEffect(parentId.toStdString(), selectedPlugin.toStdString(), pluginInfo->id);
            }

            if (onChanged) onChanged();
        });
}
