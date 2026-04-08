#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include <functional>

class FaderMeter : public juce::Component {
public:
    FaderMeter();

    void setGain(float gain);
    void setPeakLevel(float level);
    void setPeakLevelStereo(float left, float right);

    // Called when user drags the fader
    std::function<void(float newGain)> onGainChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    float gainValue = 1.0f;
    float peakL = 0.0f;
    float peakR = 0.0f;
    bool dragging = false;
    float dragStartGain = 0.0f;
    int dragStartY = 0;

    static constexpr float dbMin = -60.0f;
    static constexpr float dbMax = 6.0f;
    static constexpr float dbRange = dbMax - dbMin;
    static constexpr int faderWidth = 8;
    static constexpr int meterBarWidth = 4;
    static constexpr int meterGap = 1;
    static constexpr int meterWidth = meterBarWidth * 2 + meterGap;  // L + gap + R
    static constexpr int gap = 8;

    juce::Rectangle<int> getFaderArea() const;
    juce::Rectangle<int> getMeterArea() const;
    float gainToNormalized(float gain) const;
};
