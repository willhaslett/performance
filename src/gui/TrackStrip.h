#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <functional>

// A vertical strip representing one track in the mixer
class TrackStrip : public juce::Component {
public:
    struct PluginEntry {
        juce::String name;
        bool isInstrument;  // true = instrument, false = effect
    };

    using PluginClickCallback = std::function<void(const juce::String& trackName,
                                                     const juce::String& pluginName,
                                                     bool isInstrument)>;

    TrackStrip(const juce::String& name, PluginClickCallback onClick)
        : trackName(name), onPluginClick(std::move(onClick)) {}

    void setPlugins(const std::vector<PluginEntry>& newPlugins) {
        plugins = newPlugins;
        repaint();
    }

    void setMidiEnabled(bool enabled) {
        midiEnabled = enabled;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Background
        g.fillAll(juce::Colour(0xff1e1e1e));

        // Border
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawRect(bounds, 1);

        // Track name header
        auto header = bounds.removeFromTop(36);
        g.setColour(midiEnabled ? juce::Colour(0xff2a6aaa) : juce::Colour(0xff2a2a2a));
        g.fillRect(header);
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(trackName, header.reduced(8, 0), juce::Justification::centredLeft);

        // MIDI indicator
        if (midiEnabled) {
            auto dot = header.removeFromRight(24).withSizeKeepingCentre(8, 8);
            g.setColour(juce::Colour(0xff44cc44));
            g.fillEllipse(dot.toFloat());
        }

        // Plugin list
        bounds.removeFromTop(4);
        g.setFont(12.0f);
        for (size_t i = 0; i < plugins.size(); ++i) {
            auto row = bounds.removeFromTop(28);
            pluginBounds[i] = row;

            // Hover highlight
            if ((int)i == hoveredPlugin) {
                g.setColour(juce::Colour(0xff333333));
                g.fillRect(row);
            }

            // Icon: instrument vs effect
            g.setColour(plugins[i].isInstrument ? juce::Colour(0xffddaa44) : juce::Colour(0xff88aacc));
            g.drawText(plugins[i].isInstrument ? "I" : "F", row.removeFromLeft(24),
                       juce::Justification::centred);

            // Plugin name
            g.setColour(juce::Colour(0xffcccccc));
            g.drawText(plugins[i].name, row.reduced(4, 0), juce::Justification::centredLeft);
        }
    }

    void mouseMove(const juce::MouseEvent& event) override {
        int newHovered = -1;
        for (auto& [idx, rect] : pluginBounds) {
            if (rect.contains(event.getPosition()))
                newHovered = idx;
        }
        if (newHovered != hoveredPlugin) {
            hoveredPlugin = newHovered;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoveredPlugin != -1) {
            hoveredPlugin = -1;
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& event) override {
        for (auto& [idx, rect] : pluginBounds) {
            if (rect.contains(event.getPosition()) && idx < (int)plugins.size()) {
                if (onPluginClick)
                    onPluginClick(trackName, plugins[idx].name, plugins[idx].isInstrument);
                break;
            }
        }
    }

private:
    juce::String trackName;
    std::vector<PluginEntry> plugins;
    bool midiEnabled = true;
    PluginClickCallback onPluginClick;
    int hoveredPlugin = -1;
    std::map<int, juce::Rectangle<int>> pluginBounds;
};
