#pragma once
#include <juce_events/juce_events.h>
#include <functional>
#include <vector>
#include <cmath>

class AutomationEngine : private juce::Timer {
public:
    using Callback = std::function<void(float value)>;
    using EasingFn = std::function<float(float t)>;

    AutomationEngine();
    ~AutomationEngine() override;

    // Interpolate from → to over durationSec, calling callback with each
    // value. `onComplete` fires after the final value is delivered (optional).
    // Returns a handle for cancellation.
    int interpolate(float from, float to, float durationSec,
                    Callback callback, EasingFn easing = nullptr,
                    std::function<void()> onComplete = {});

    // One-shot delay: call callback after delaySec seconds.
    int delay(float delaySec, std::function<void()> callback);

    void cancel(int handle);
    void cancelAll();

    // Built-in easing functions
    static float easeLinear(float t)  { return t; }
    static float easeIn(float t)     { return t * t; }
    static float easeOut(float t)    { return t * (2.0f - t); }
    static float easeCosine(float t) { return (1.0f - std::cos(t * 3.14159265f)) * 0.5f; }
    static float easeSCurve(float t) { return t * t * (3.0f - 2.0f * t); }

    // Resolve a named easing to a function
    static EasingFn easingByName(const std::string& name);

private:
    void timerCallback() override;

    struct ActiveAutomation {
        int id;
        float from, to;
        Callback callback;
        EasingFn easing;
        double startTime;
        double duration;
        bool isDelay = false;
        std::function<void()> delayCallback;
        std::function<void()> onComplete;  // interpolate: fires after final value
    };

    std::vector<ActiveAutomation> active;
    int nextId = 1;
};
