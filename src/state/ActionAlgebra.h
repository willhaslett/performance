#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// Action algebra — a compositional representation of what an action "does,"
// documented in docs/ACTION_ALGEBRA.md. The tree is pure data: no function
// pointers, no Lua strings (except the deliberate Lua escape-hatch op).
// An interpreter (next commit) walks the tree, driving AutomationEngine
// for time-varying ops.

namespace ActionAlgebra {

// Target — mutable state slot that Set/Interpolate can read or write.
// Each kind is interpreted by the runtime against StateAPI.
struct Target {
    enum class Kind {
        TrackGain,     // entityId = trackId
        BusGain,       // entityId = busId
        MasterGain,    // no entityId
        TrackParam,    // entityId = trackId, paramIndex = plugin param index
        Selection,     // entityId = trackId to select (empty = clear selection)
    };
    Kind        kind = Kind::TrackGain;
    std::string entityId;
    int         paramIndex = -1;

    bool operator==(const Target&) const;
    bool operator!=(const Target& o) const { return !(*this == o); }
};

// Value — what Set writes, or the endpoints / duration of Interpolate.
//
//   Number          — concrete double.
//   Placeholder     — template hole; substituted at expansion time with the
//                     bound arg value. Name can be a schema param name
//                     ("trackName") or a well-known placeholder ("value" for
//                     the MIDI CC float passed to bindings).
//   CaptureCurrent  — Interpolate-only; means "read the target's current
//                     value at execution start." Lets a template model
//                     "fade from wherever we are now" without baking the
//                     start value into the tree.
struct Value {
    enum class Kind { Number, Text, Placeholder, CaptureCurrent };
    Kind        kind = Kind::Number;
    double      number = 0.0;
    std::string text;         // Kind::Text — UUID, enum value, "Main", etc.
    std::string placeholder;

    bool operator==(const Value&) const;
    bool operator!=(const Value& o) const { return !(*this == o); }
};

// Convenience Value constructors — make hand-built trees readable.
inline Value num(double v)                 { return { Value::Kind::Number, v, {}, {} }; }
inline Value text(std::string s)           { return { Value::Kind::Text, 0.0, std::move(s), {} }; }
inline Value placeholder(std::string name) { return { Value::Kind::Placeholder, 0.0, {}, std::move(name) }; }
inline Value captureCurrent()              { return { Value::Kind::CaptureCurrent, 0.0, {}, {} }; }

// ActionNode — the algebra tree. See docs/ACTION_ALGEBRA.md for semantics.
struct ActionNode {
    enum class Op {
        Set,         // target <- to
        Interpolate, // target : from -> to over duration with easing
        Delay,       // wait duration, then run children[0]
        Parallel,    // run every child concurrently
        Sequence,    // run children one after another
        Invoke,      // expand a named action with args
        Lua,         // run luaCode with bound args + value in scope (escape hatch)
    };

    Op     op = Op::Set;

    // Set / Interpolate
    Target target;
    Value  from;      // Interpolate only (may be CaptureCurrent)
    Value  to;
    Value  duration;  // Interpolate / Delay (seconds)
    std::string easing;  // named curve, resolved by AutomationEngine at run time

    // Parallel / Sequence / Delay
    std::vector<ActionNode> children;

    // Invoke
    std::string         invokeName;
    std::vector<Value>  invokeArgs;

    // Lua
    std::string luaCode;

    bool operator==(const ActionNode&) const;
    bool operator!=(const ActionNode& o) const { return !(*this == o); }
};

// --- Builders (readable C++ construction) ---

ActionNode set(Target t, Value v);
ActionNode interpolate(Target t, Value from, Value to, Value duration,
                        std::string easing = "linear");
ActionNode delay(Value duration, ActionNode child);
ActionNode parallel(std::vector<ActionNode> children);
ActionNode sequence(std::vector<ActionNode> children);
ActionNode invoke(std::string name, std::vector<Value> args);
ActionNode lua(std::string code);

// --- Placeholder substitution ---

// Walk `tree`; replace every `Placeholder` whose name matches a key in
// `bindings` with the corresponding Value. Unbound placeholders are left
// in place (the interpreter rejects them with an explicit error — a bug
// in either the template or the args).
ActionNode substitute(const ActionNode& tree,
                       const std::unordered_map<std::string, Value>& bindings);

// --- JSON round-trip ---

std::string toJson(const ActionNode& node);
ActionNode  fromJson(const std::string& json);

}  // namespace ActionAlgebra
