#include "state/ActionAlgebra.h"
#include <juce_core/juce_core.h>

namespace ActionAlgebra {

// --- Equality ---

bool Target::operator==(const Target& o) const {
    return kind == o.kind && entityId == o.entityId && paramIndex == o.paramIndex;
}

bool Value::operator==(const Value& o) const {
    if (kind != o.kind) return false;
    switch (kind) {
        case Kind::Number:         return number == o.number;
        case Kind::Placeholder:    return placeholder == o.placeholder;
        case Kind::CaptureCurrent: return true;
    }
    return false;
}

bool ActionNode::operator==(const ActionNode& o) const {
    if (op != o.op) return false;
    switch (op) {
        case Op::Set:
            return target == o.target && to == o.to;
        case Op::Interpolate:
            return target == o.target && from == o.from && to == o.to
                && duration == o.duration && easing == o.easing;
        case Op::Delay:
            return duration == o.duration && children == o.children;
        case Op::Parallel:
        case Op::Sequence:
            return children == o.children;
        case Op::Invoke:
            return invokeName == o.invokeName && invokeArgs == o.invokeArgs;
        case Op::Lua:
            return luaCode == o.luaCode;
    }
    return false;
}

// --- Builders ---

ActionNode set(Target t, Value v) {
    ActionNode n;
    n.op = ActionNode::Op::Set;
    n.target = std::move(t);
    n.to = std::move(v);
    return n;
}

ActionNode interpolate(Target t, Value from, Value to, Value duration, std::string easing) {
    ActionNode n;
    n.op = ActionNode::Op::Interpolate;
    n.target = std::move(t);
    n.from = std::move(from);
    n.to = std::move(to);
    n.duration = std::move(duration);
    n.easing = std::move(easing);
    return n;
}

ActionNode delay(Value duration, ActionNode child) {
    ActionNode n;
    n.op = ActionNode::Op::Delay;
    n.duration = std::move(duration);
    n.children.push_back(std::move(child));
    return n;
}

ActionNode parallel(std::vector<ActionNode> children) {
    ActionNode n;
    n.op = ActionNode::Op::Parallel;
    n.children = std::move(children);
    return n;
}

ActionNode sequence(std::vector<ActionNode> children) {
    ActionNode n;
    n.op = ActionNode::Op::Sequence;
    n.children = std::move(children);
    return n;
}

ActionNode invoke(std::string name, std::vector<Value> args) {
    ActionNode n;
    n.op = ActionNode::Op::Invoke;
    n.invokeName = std::move(name);
    n.invokeArgs = std::move(args);
    return n;
}

ActionNode lua(std::string code) {
    ActionNode n;
    n.op = ActionNode::Op::Lua;
    n.luaCode = std::move(code);
    return n;
}

// --- Substitution ---

static Value substituteValue(const Value& v,
                              const std::unordered_map<std::string, Value>& bindings) {
    if (v.kind != Value::Kind::Placeholder) return v;
    auto it = bindings.find(v.placeholder);
    return it != bindings.end() ? it->second : v;
}

ActionNode substitute(const ActionNode& tree,
                       const std::unordered_map<std::string, Value>& bindings) {
    ActionNode out = tree;
    out.from     = substituteValue(out.from, bindings);
    out.to       = substituteValue(out.to, bindings);
    out.duration = substituteValue(out.duration, bindings);
    for (auto& a : out.invokeArgs) a = substituteValue(a, bindings);
    for (auto& c : out.children)   c = substitute(c, bindings);
    return out;
}

// --- JSON ---
//
// Format mirrors the surface we want LLMs + persistence to work with.
// Compact: only non-default fields emitted. Example trees:
//
//   fadeOut(track, duration, easing):
//     {"op":"interpolate","target":{"kind":"trackGain","entityId":{"$":"trackName"}},
//      "from":":current","to":0,"duration":{"$":"duration"},"easing":{"$":"easing"}}
//
//   crossfade:
//     {"op":"parallel","children":[fadeOut,fadeIn]}
//
// Placeholders and :current are distinguished JSON shapes so we don't
// confuse them with numbers / strings at parse time.

static juce::var targetKindToString(Target::Kind k) {
    switch (k) {
        case Target::Kind::TrackGain:  return "trackGain";
        case Target::Kind::BusGain:    return "busGain";
        case Target::Kind::MasterGain: return "masterGain";
        case Target::Kind::TrackParam: return "trackParam";
        case Target::Kind::Selection:  return "selection";
    }
    return "trackGain";
}

static Target::Kind targetKindFromString(const juce::String& s) {
    if (s == "trackGain")  return Target::Kind::TrackGain;
    if (s == "busGain")    return Target::Kind::BusGain;
    if (s == "masterGain") return Target::Kind::MasterGain;
    if (s == "trackParam") return Target::Kind::TrackParam;
    if (s == "selection")  return Target::Kind::Selection;
    return Target::Kind::TrackGain;
}

static juce::var valueToVar(const Value& v) {
    switch (v.kind) {
        case Value::Kind::Number:
            return v.number;
        case Value::Kind::CaptureCurrent:
            return ":current";
        case Value::Kind::Placeholder: {
            auto* o = new juce::DynamicObject();
            o->setProperty("$", juce::String(v.placeholder));
            return juce::var(o);
        }
    }
    return juce::var();
}

static Value valueFromVar(const juce::var& v) {
    if (v.isString()) {
        if (v.toString() == ":current") return captureCurrent();
        // Bare strings outside the ":current" sentinel shouldn't appear in Values
        // today (placeholders always use the {"$": name} form). Treat as
        // placeholder for forgiveness.
        return placeholder(v.toString().toStdString());
    }
    if (auto* obj = v.getDynamicObject()) {
        auto ph = obj->getProperty("$").toString();
        if (ph.isNotEmpty()) return placeholder(ph.toStdString());
    }
    if (v.isDouble() || v.isInt() || v.isInt64()) {
        return num((double)v);
    }
    return num(0.0);
}

static juce::var targetToVar(const Target& t) {
    auto* o = new juce::DynamicObject();
    o->setProperty("kind", targetKindToString(t.kind));
    if (!t.entityId.empty()) {
        // entityId can itself be a placeholder — stored as a plain string or
        // a {"$": name} object. Support both for symmetry with Value.
        o->setProperty("entityId", juce::String(t.entityId));
    }
    if (t.paramIndex >= 0)
        o->setProperty("paramIndex", t.paramIndex);
    return juce::var(o);
}

static Target targetFromVar(const juce::var& v) {
    Target t;
    if (auto* obj = v.getDynamicObject()) {
        t.kind = targetKindFromString(obj->getProperty("kind").toString());
        t.entityId = obj->getProperty("entityId").toString().toStdString();
        auto pi = obj->getProperty("paramIndex");
        if (!pi.isVoid()) t.paramIndex = (int)pi;
    }
    return t;
}

static const char* opToString(ActionNode::Op op) {
    switch (op) {
        case ActionNode::Op::Set:         return "set";
        case ActionNode::Op::Interpolate: return "interpolate";
        case ActionNode::Op::Delay:       return "delay";
        case ActionNode::Op::Parallel:    return "parallel";
        case ActionNode::Op::Sequence:    return "sequence";
        case ActionNode::Op::Invoke:      return "invoke";
        case ActionNode::Op::Lua:         return "lua";
    }
    return "set";
}

static ActionNode::Op opFromString(const juce::String& s) {
    if (s == "set")         return ActionNode::Op::Set;
    if (s == "interpolate") return ActionNode::Op::Interpolate;
    if (s == "delay")       return ActionNode::Op::Delay;
    if (s == "parallel")    return ActionNode::Op::Parallel;
    if (s == "sequence")    return ActionNode::Op::Sequence;
    if (s == "invoke")      return ActionNode::Op::Invoke;
    if (s == "lua")         return ActionNode::Op::Lua;
    return ActionNode::Op::Set;
}

static juce::var nodeToVar(const ActionNode& n) {
    auto* o = new juce::DynamicObject();
    o->setProperty("op", juce::String(opToString(n.op)));

    switch (n.op) {
        case ActionNode::Op::Set:
            o->setProperty("target", targetToVar(n.target));
            o->setProperty("to", valueToVar(n.to));
            break;
        case ActionNode::Op::Interpolate:
            o->setProperty("target", targetToVar(n.target));
            o->setProperty("from", valueToVar(n.from));
            o->setProperty("to", valueToVar(n.to));
            o->setProperty("duration", valueToVar(n.duration));
            if (!n.easing.empty()) o->setProperty("easing", juce::String(n.easing));
            break;
        case ActionNode::Op::Delay: {
            o->setProperty("duration", valueToVar(n.duration));
            juce::var kids;
            for (auto& c : n.children) kids.append(nodeToVar(c));
            o->setProperty("children", kids);
            break;
        }
        case ActionNode::Op::Parallel:
        case ActionNode::Op::Sequence: {
            juce::var kids;
            for (auto& c : n.children) kids.append(nodeToVar(c));
            o->setProperty("children", kids);
            break;
        }
        case ActionNode::Op::Invoke: {
            o->setProperty("name", juce::String(n.invokeName));
            juce::var args;
            for (auto& a : n.invokeArgs) args.append(valueToVar(a));
            o->setProperty("args", args);
            break;
        }
        case ActionNode::Op::Lua:
            o->setProperty("code", juce::String(n.luaCode));
            break;
    }
    return juce::var(o);
}

static ActionNode nodeFromVar(const juce::var& v) {
    ActionNode n;
    if (auto* obj = v.getDynamicObject()) {
        n.op = opFromString(obj->getProperty("op").toString());
        switch (n.op) {
            case ActionNode::Op::Set:
                n.target = targetFromVar(obj->getProperty("target"));
                n.to     = valueFromVar(obj->getProperty("to"));
                break;
            case ActionNode::Op::Interpolate:
                n.target   = targetFromVar(obj->getProperty("target"));
                n.from     = valueFromVar(obj->getProperty("from"));
                n.to       = valueFromVar(obj->getProperty("to"));
                n.duration = valueFromVar(obj->getProperty("duration"));
                n.easing   = obj->getProperty("easing").toString().toStdString();
                break;
            case ActionNode::Op::Delay:
                n.duration = valueFromVar(obj->getProperty("duration"));
                if (auto* kids = obj->getProperty("children").getArray())
                    for (auto& k : *kids) n.children.push_back(nodeFromVar(k));
                break;
            case ActionNode::Op::Parallel:
            case ActionNode::Op::Sequence:
                if (auto* kids = obj->getProperty("children").getArray())
                    for (auto& k : *kids) n.children.push_back(nodeFromVar(k));
                break;
            case ActionNode::Op::Invoke:
                n.invokeName = obj->getProperty("name").toString().toStdString();
                if (auto* args = obj->getProperty("args").getArray())
                    for (auto& a : *args) n.invokeArgs.push_back(valueFromVar(a));
                break;
            case ActionNode::Op::Lua:
                n.luaCode = obj->getProperty("code").toString().toStdString();
                break;
        }
    }
    return n;
}

std::string toJson(const ActionNode& node) {
    return juce::JSON::toString(nodeToVar(node), true).toStdString();
}

ActionNode fromJson(const std::string& json) {
    return nodeFromVar(juce::JSON::parse(juce::String(json)));
}

}  // namespace ActionAlgebra
