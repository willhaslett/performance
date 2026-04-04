#include "automation/AutomationEngine.h"
#include "engine/Log.h"

AutomationEngine::AutomationEngine() {
    startTimerHz(60);
}

AutomationEngine::~AutomationEngine() {
    stopTimer();
}

int AutomationEngine::interpolate(float from, float to, float durationSec,
                                    Callback callback, EasingFn easing) {
    int id = nextId++;
    ActiveAutomation a;
    a.id = id;
    a.from = from;
    a.to = to;
    a.callback = std::move(callback);
    a.easing = easing ? std::move(easing) : EasingFn(easeLinear);
    a.startTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    a.duration = durationSec;
    active.push_back(std::move(a));
    perfLog("[Automation] Started interpolation %d: %.2f -> %.2f over %.2fs\n",
            id, from, to, durationSec);
    return id;
}

int AutomationEngine::delay(float delaySec, std::function<void()> callback) {
    int id = nextId++;
    ActiveAutomation a;
    a.id = id;
    a.from = 0;
    a.to = 0;
    a.isDelay = true;
    a.delayCallback = std::move(callback);
    a.startTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    a.duration = delaySec;
    active.push_back(std::move(a));
    return id;
}

void AutomationEngine::cancel(int handle) {
    active.erase(
        std::remove_if(active.begin(), active.end(),
                        [handle](const ActiveAutomation& a) { return a.id == handle; }),
        active.end());
}

void AutomationEngine::cancelAll() {
    active.clear();
}

AutomationEngine::EasingFn AutomationEngine::easingByName(const std::string& name) {
    if (name == "easein")  return easeIn;
    if (name == "easeout") return easeOut;
    if (name == "cosine")  return easeCosine;
    if (name == "scurve")  return easeSCurve;
    return easeLinear;
}

void AutomationEngine::timerCallback() {
    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    for (auto it = active.begin(); it != active.end(); ) {
        double elapsed = now - it->startTime;
        double t = (it->duration > 0) ? (elapsed / it->duration) : 1.0;

        if (t >= 1.0) {
            // Complete
            if (it->isDelay) {
                if (it->delayCallback) it->delayCallback();
            } else {
                if (it->callback) it->callback(it->to);
            }
            it = active.erase(it);
        } else {
            // In progress
            if (!it->isDelay && it->callback) {
                float easedT = it->easing ? it->easing(static_cast<float>(t)) : static_cast<float>(t);
                float value = it->from + (it->to - it->from) * easedT;
                it->callback(value);
            }
            ++it;
        }
    }
}
