# Action Algebra — A Tiny Compositional Core

Replace the ad-hoc action-handler dispatch ladder with a small algebra of
primitives and a tree interpreter. Every current built-in (`fadeOut`,
`crossfade`, `morph`, etc.) becomes a labeled template that expands to an
algebra tree at trigger time. The dispatcher disappears; one interpreter
runs everything.

Builds directly on the typed `ParamSchema` work in
`docs/ACTION_INSTANCES_REFACTOR.md`. The schema is how templates declare
their parameter shape; the algebra is what their body looks like.

> Read this doc before touching `executeAction`, the action registration
> code, or the stored action-event/binding format. The primitive set is
> deliberately small; resist adding to it unless a concrete need (not an
> imagined one) forces it.

## Why this work

Two smells that the action-instances refactor surfaced but didn't fix:

1. **Built-in actions are a closed set in execution.** `executeAction` has
   eight hardcoded `if (name == "...") { ... }` branches, each with bespoke
   C++ using `AutomationEngine` + `StateAPI`. The catalog is open
   (registration is just `push_back`), but adding behavior means editing
   that ladder.

2. **Compounds (`morph`) are a second, parallel mechanism.** Morph dispatches
   sub-actions with parallel/sequential semantics inside `executeAction`,
   reinventing composition that should be a first-class primitive.

A third observation: **offline render (bounce) and real-time trigger are
almost the same thing** — they both walk an action tree and schedule
interpolations. Today bounce has to freeze automation because the real-time
scheduler is entangled with the message thread. An algebra + interpreter
separates the tree from the scheduler, which makes both offline and
real-time fall out naturally.

## Design — the model after

### The six primitives

```cpp
struct Target;       // see below — what we set or interpolate

struct ActionNode {
    enum class Op {
        Set,          // Target <- Value
        Interpolate,  // Target : from -> to over duration with easing
        Delay,        // wait, then run child
        Parallel,     // run all children concurrently
        Sequence,     // run children one after another
        Invoke,       // expand a named action with args
    };

    Op op;

    // Op::Set / Op::Interpolate
    Target  target;
    Value   from;       // Interpolate only; may be "capture current at start"
    Value   to;         // Set: the value. Interpolate: the destination.
    double  duration;   // Interpolate / Delay (seconds)
    Easing  easing;

    // Op::Delay / Op::Parallel / Op::Sequence
    std::vector<ActionNode> children;

    // Op::Invoke
    std::string   invokeName;
    std::vector<Value> invokeArgs;
};
```

Everything is value-typed, copyable, serializable. No function pointers, no
Lua strings. The tree is the program.

### Targets

A small sum type over the settable/interpolatable state:

```cpp
struct Target {
    enum class Kind { TrackGain, BusGain, MasterGain, TrackParam };
    Kind   kind;
    std::string entityId;   // trackId / busId (unused for MasterGain)
    int    paramIndex = -1; // TrackParam only
};
```

Four kinds to start. Each has read/write semantics the interpreter knows.
New kinds (send gain, tempo, plugin bypass) get added when a concrete
action needs them — not speculatively.

### Values and "capture current at start"

```cpp
struct Value {
    enum class Kind { Number, Placeholder, CaptureCurrent };
    Kind   kind;
    double number      = 0.0;   // Kind::Number
    std::string  placeholder;   // Kind::Placeholder: $paramName (substituted at expansion)
};
```

`CaptureCurrent` means "read this target's value at the moment the
Interpolate starts." It's how `fadeOut` models "fade from wherever we are
now" without baking the current value into the template.

Placeholders let templates reference their args by name:
`{kind: Placeholder, placeholder: "duration"}` gets substituted with the
concrete arg value when the template is expanded.

### Actions as labeled templates

An `ActionInfo` gains a body alongside its existing `params`:

```cpp
struct ActionInfo {
    ActionId    id;
    std::string name;
    std::string label;
    std::vector<ParamSchema> params;
    ActionNode  body;           // the template tree; placeholders name params
    SongId      songId;
    int durationParamIndex = -1;
};
```

The `luaCode` field is *gone* — named actions are now pure data. The
escape hatch for procedural logic moves to a dedicated Op below.

All eight current built-ins become bodies:

- `setActiveTrack` → `Set(Selection, $trackName)` (selection needs its own
  target kind; see "New target kinds" below)
- `fadeOut` → `Interpolate(TrackGain($track), :current, 0, $duration, $easing)`
- `fadeIn`  → `Interpolate(TrackGain($track), :current, 1, $duration, $easing)`
- `crossfade` → `Parallel([fadeOut, fadeIn])`
- `trackVolume` → `Set(ChannelGain($channel), cube($value))` — needs `$value`
  (the MIDI CC float) which is a well-known placeholder distinct from
  schema args. See "MIDI value placeholder" below.
- `morphToPreset` → `Parallel([Interpolate(TrackParam($track, 0), :current, $preset.params[0], $dur, $easing), ...])`
  — param count not known until expansion time. See "Runtime-sized trees."
- `morphChain` → `Sequence([morphToPreset(A), Delay($dwell, empty), morphToPreset(B)])`
- `morph` (the old compound) → the body IS a tree with children. The morph
  editor becomes a lightweight editor for the body directly.

### Runtime-sized trees

`morphToPreset` expands to a Parallel whose child count depends on the
plugin's live parameter count. The template can't be static; it needs to
be *computed* at expansion time from state.

Two ways this could work:

1. **Expansion functions**. A named action optionally has a C++ expander
   `(args, state) → ActionNode` that builds the tree dynamically. Used for
   the morph family. Most actions don't need this — they're static bodies
   with placeholder substitution.

2. **A `ForEach` primitive**. `ForEach(source, body)` where `source` is
   something like `{kind: "presetParams", track: $track, preset: $preset}`
   and `body` is the per-iteration subtree. Pure data, no C++.

(1) is simpler. (2) is more principled. **Going with (1) first** — if we
find ourselves writing more than two or three expanders, promote (2).
Parsimony wins for v1.

### The `Lua` escape hatch

Still useful for genuinely procedural needs (conditionals, queries,
multi-step logic that doesn't fit the algebra). Add it as a seventh op:

```cpp
Op::Lua   // child is a std::string of Lua code, executed with args + value in scope
```

The Lua body can emit more algebra (via a new Lua binding that enqueues
subtrees), or just do side effects directly with existing bindings.

Most user-defined actions will use this for a long time. The built-ins
won't need it.

### MIDI value placeholder

Bindings from continuous controls (CC faders, pitch-bend) pass a `value`
in [0, 1] alongside the args. The algebra treats this as a distinguished
placeholder `$value`. Trigger actions (notes, pads) pass `$value = 1`.
`$value = 0` remains a cancel for non-continuous actions (matching
today's `continuousActions` set).

### Interpreter

Takes an `ActionNode`, a scheduler (wraps `AutomationEngine`), and the
state. Walks the tree:

- `Set`: `target.write(state, to)`. Synchronous.
- `Interpolate`: resolve `from` (:current reads from target), register
  with `AutomationEngine`. Returns a handle.
- `Delay(d, child)`: `AutomationEngine.delay(d, () -> run(child))`.
- `Parallel(xs)`: run all immediately.
- `Sequence(xs)`: run `xs[0]`, on completion run `xs[1]`, ...
  (`AutomationEngine` already exposes completion callbacks.)
- `Invoke(name, args)`: look up `name`, substitute `args` into the body's
  placeholders, recurse.
- `Lua(code)`: fall back to LuaEngine with `args` + `value` in scope.

The interpreter is ~100 lines. Cancellation is per-handle as today; a
`cancel` on a composite cancels all scheduled descendants.

### Offline render (bounce) compatibility

Today's bounce freezes automation because interpolations are pinned to
the message thread's wall clock. With an algebra, bounce runs the same
interpreter against a *virtual clock* — `Interpolate(d=3s)` advances 3
seconds of beats at whatever render rate we want. No special-casing; the
tree doesn't know the difference.

This lifts two of the bounce caveats in CLAUDE.md ("automation values
freeze during render" and partially the tempo-change one).

### Serialization

`ActionNode` serializes to JSON. The body goes in the `actions` table
next to (or instead of) the existing Lua code column. Action-event and
binding storage is unchanged — they still just carry `(actionId,
argsJson)`. The interpreter does `body + args → concrete tree` at trigger
time.

## Build sequence

Each step a commit; app builds + tests pass at every step. Roll back
rather than patch-forward.

**Status as of 2026-04-20**: steps 1–8 landed on branch
`action-algebra`. Step 9 (bounce + virtual clock) deferred to
post-0.1.0 — the core refactor goal (kill the dispatch ladder, unify
execution) is done without it. Bounce continues to run with frozen
automation until step 9 lands; no regression from the pre-refactor
baseline. `state.db` reset is required because the actions table
gained a `body_json` column.

1. **`ActionNode` + `Target` + `Value` types.** Header-only. Include a
   `toJson` / `fromJson` round-trip (mirrors `ParamSchemaJson`). Unit
   tests for round-trip and placeholder substitution.
2. **Interpreter.** `ActionInterpreter::run(node, args, value)`. Covers
   Set, Interpolate, Delay, Parallel, Sequence. No `Invoke` or `Lua`
   yet. Unit tests with a `MockScheduler`.
3. **`Invoke` + expansion.** Looks up named actions, substitutes
   placeholders. Recursive. Tested with a hand-built template.
4. **Migrate simple built-ins.** `setActiveTrack`, `fadeOut`, `fadeIn`,
   `crossfade`, `trackVolume`. Each registered with an `ActionNode`
   body instead of a C++ handler branch. Dispatch ladder shrinks.
5. **Expansion-function hook.** Named actions may optionally provide
   a `(args, state) → ActionNode` expander. Used for morph family.
6. **Migrate morph family.** `morphToPreset`, `morphChain`, `morph`
   (the compound) all become interpreter-driven. Dispatch ladder
   disappears; `executeAction` becomes six lines.
7. **`Lua` primitive.** New `Op::Lua`. Existing custom-Lua actions
   migrate transparently — their body becomes a single-node `Lua`
   tree wrapping the existing luaCode.
8. **Persistence.** Action bodies round-trip as JSON.
   `luaCode` column goes away (its content lives inside a `Lua` node
   in the body).
9. **Bounce integration.** Offline renderer uses the interpreter with
   a virtual clock. Resolves the "automation freezes during bounce"
   caveat.

## Out of scope

- **Full generic tree editor UI.** The MorphEditor stays as-is for now
  (it edits morph bodies via its current UX). A general "compose any
  action tree" editor is a later project; won't block this refactor.
- **New primitive ops beyond the seven.** No `ForEach`, no
  conditionals, no loops. If we need procedural logic, it goes in a
  `Lua` node.
- **New target kinds** we don't have an action for today. Track gain,
  bus gain, master gain, track param cover everything the current
  built-ins touch. `Selection` target is needed for `setActiveTrack`;
  that's step 4's single addition.
- **Typed value system** beyond number + placeholder + :current. If we
  ever need discrete target writes (e.g., "set bypass = true"), we'll
  add `BooleanValue`. Not today.

## Open questions

- **Timing within 0.1.0 or defer.** This is a bigger refactor than the
  typed-schema work. Full scope is ~2 focused days. The crash class
  that motivated the previous refactor is fixed; this is pure
  architectural cleanup with payoff in bounce + future extensibility.
  Asymmetry argument: if we don't do this now, we're picking between
  "all built-ins in C++" (T0) and "all built-ins in Lua" (T1); both
  are deliberately uglier than T2 (algebra). Leaning toward *before*
  0.1.0 so we don't ship a shape we know we want to change.
- **Expander functions in C++ vs ForEach primitive.** Going with C++
  expanders for now (simpler); revisit if we write more than three.
- **Selection as a target.** `setActiveTrack` wants `Set(Selection,
  trackId)`. Is selection really a "target" (mutable state) or
  something else? For v1 yes — it has a single current value that an
  action sets. If it ever becomes more structured, reclassify then.
