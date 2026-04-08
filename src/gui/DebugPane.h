#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "gui/Theme.h"
#include <deque>
#include <vector>
#include <string>
#include <mutex>

class PerformanceCoordinator;
class EngineAPI;

class DebugPane : public juce::Component, private juce::Timer {
public:
    DebugPane(PerformanceCoordinator& coordinator, EngineAPI& engine);
    ~DebugPane() override;

    void activate();    // start monitoring
    void deactivate();  // stop monitoring

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    PerformanceCoordinator& coordinator;
    EngineAPI& engine;
    bool active = false;

    // MIDI event log
    struct MidiEvent {
        std::string deviceName;
        std::string description;
        std::string type;
        int channel;
        int number;
        int value;
        double timestamp;  // seconds since activation
    };
    std::deque<MidiEvent> midiEvents;
    static constexpr int maxMidiEvents = 200;
    int midiScrollOffset = 0;
    double activationTime = 0.0;

    // Audio input levels
    std::vector<float> inputLevels;
    std::vector<juce::String> inputNames;

    static constexpr int headerHeight = 30;
    static constexpr int midiRowHeight = 18;
    static constexpr int meterHeight = 12;
    static constexpr int meterGap = 4;
    static constexpr int audioPanelHeight = 120;

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
};
