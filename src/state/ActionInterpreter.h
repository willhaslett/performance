#pragma once
#include "state/ActionAlgebra.h"
#include <functional>

// ActionInterpreter — walks an ActionAlgebra tree and drives a Scheduler
// (for time-varying ops) and TargetIO (for reading/writing state targets).
//
// Scheduler and TargetIO are abstract so the interpreter is testable with
// mocks and reusable for bounce/offline render against a virtual clock.
// Real implementations wrap AutomationEngine + StateAPI respectively.
//
// See docs/ACTION_ALGEBRA.md for semantics.

namespace ActionAlgebra {

class ActionInterpreter {
public:
    // Schedules time-varying work. Real impl wraps AutomationEngine. Mock
    // impl in tests records calls and fires completions on demand.
    struct Scheduler {
        virtual ~Scheduler() = default;
        virtual void interpolate(float from, float to, float durationSec,
                                  std::function<void(float)> onTick,
                                  std::function<void()>       onComplete,
                                  const std::string&          easingName) = 0;
        virtual void delay(float durationSec, std::function<void()> onComplete) = 0;
    };

    // Reads and writes target slots. Real impl wraps StateAPI. Mock impl in
    // tests is a map.
    struct TargetIO {
        virtual ~TargetIO() = default;
        virtual float read(const Target& t) = 0;
        virtual void  write(const Target& t, float value) = 0;
    };

    ActionInterpreter(Scheduler& scheduler, TargetIO& io);

    // Run a tree. `onComplete` (optional) fires when the entire tree
    // finishes (every Interpolate ended, every Delay elapsed, every
    // Sequence child exhausted). Fires synchronously from inside Scheduler
    // callbacks for time-varying ops; for purely synchronous trees (e.g.
    // a single Set) it fires before run() returns.
    void run(const ActionNode& node, std::function<void()> onComplete = {});

private:
    Scheduler& scheduler;
    TargetIO&  io;

    // Helpers
    float resolveValue(const Value& v, const Target* captureTarget);
    void  runSequence(const std::vector<ActionNode>& children, size_t index,
                      std::function<void()> onComplete);
};

}  // namespace ActionAlgebra
