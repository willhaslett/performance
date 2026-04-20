#include "state/ActionInterpreter.h"
#include "engine/Log.h"
#include <memory>
#include <atomic>

namespace ActionAlgebra {

ActionInterpreter::ActionInterpreter(Scheduler& s, TargetIO& i)
    : scheduler(s), io(i) {}

float ActionInterpreter::resolveValue(const Value& v, const Target* captureTarget) {
    switch (v.kind) {
        case Value::Kind::Number:
            return (float)v.number;
        case Value::Kind::CaptureCurrent:
            return captureTarget ? io.read(*captureTarget) : 0.0f;
        case Value::Kind::Placeholder:
            // Unbound placeholder — programmer error in the template or args.
            // Per fail-hard, log and return 0; the assertion comes when someone
            // calls a typed API with a resolved UUID this Value should've held.
            perfLog("[ActionInterpreter] Unbound placeholder '%s'\n",
                    v.placeholder.c_str());
            return 0.0f;
    }
    return 0.0f;
}

static void fire(const std::function<void()>& f) { if (f) f(); }

void ActionInterpreter::run(const ActionNode& node,
                              std::function<void()> onComplete) {
    switch (node.op) {
        case ActionNode::Op::Set: {
            float v = resolveValue(node.to, &node.target);
            io.write(node.target, v);
            fire(onComplete);
            return;
        }

        case ActionNode::Op::Interpolate: {
            float from = resolveValue(node.from, &node.target);
            float to   = resolveValue(node.to,   &node.target);
            float dur  = resolveValue(node.duration, nullptr);
            auto target = node.target;
            scheduler.interpolate(from, to, dur,
                [this, target](float v) { io.write(target, v); },
                [onComplete]() { fire(onComplete); },
                node.easing);
            return;
        }

        case ActionNode::Op::Delay: {
            float dur = resolveValue(node.duration, nullptr);
            if (node.children.empty()) {
                scheduler.delay(dur, [onComplete]() { fire(onComplete); });
                return;
            }
            // Wait, then run the child with the completion forwarded.
            auto child = node.children[0];
            scheduler.delay(dur, [this, child, onComplete]() {
                run(child, onComplete);
            });
            return;
        }

        case ActionNode::Op::Parallel: {
            if (node.children.empty()) { fire(onComplete); return; }
            auto remaining = std::make_shared<std::atomic<int>>((int)node.children.size());
            for (const auto& child : node.children) {
                run(child, [remaining, onComplete]() {
                    if (--(*remaining) == 0) fire(onComplete);
                });
            }
            return;
        }

        case ActionNode::Op::Sequence:
            runSequence(node.children, 0, std::move(onComplete));
            return;

        case ActionNode::Op::Invoke:
        case ActionNode::Op::Lua:
            // Not handled in step 2 — step 3 wires Invoke with template
            // expansion, step 7 wires Lua. For now, log + no-op complete.
            perfLog("[ActionInterpreter] Op %d not yet implemented\n", (int)node.op);
            fire(onComplete);
            return;
    }
}

void ActionInterpreter::runSequence(const std::vector<ActionNode>& children,
                                     size_t index,
                                     std::function<void()> onComplete) {
    if (index >= children.size()) { fire(onComplete); return; }
    auto allChildren = children;  // own a copy for the nested callback
    run(children[index], [this, allChildren, index, onComplete]() {
        runSequence(allChildren, index + 1, onComplete);
    });
}

}  // namespace ActionAlgebra
