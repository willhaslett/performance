# Action-Instances Refactor — Typed Schemas, One Form, Cascade-Delete

This is a full pass over how the app lets a user instantiate an action
— both as an event on an action track and as a MIDI binding — plus
what happens to those instances when the entities they reference get
deleted. Triggered by a crash where a saved `crossfade` binding had an
empty `toTrack` UUID (see `DEV_HISTORY.md`): the creation UI accepted a
blank required field, and the executing action tripped the fail-loud
assertion in `StateAPI::track()`.

The fail-loud assertion did its job. The creation UI didn't.

> Read this doc before touching action-registration, action-instance
> creation UI, or the param schema format. The primitives here are
> deliberately small; resist adding to them unless a concrete need
> (not an imagined one) forces it.

## Why this work

Three related problems, all rooted in the same weak core:

1. **`paramSchema` is a string-typed JSON blob with a four-value grammar.**
   Types are `string | float | channel | morph`. Every track, bus, and
   preset reference is stored as `"string"`. Every easing enum value is
   stored as `"string"`. The schema can't distinguish a UUID reference
   from free text from an enum value.

2. **Three independent "create an action instance" forms.** ProducePane
   (action-track event), MorphEditor (sub-action in a compound), and
   SongMappingsPane (MIDI binding) each parse the schema slightly
   differently and each render a different UI. The "pick a track"
   submenu only exists in ProducePane's first-arg flow. Remaining args
   fall through to `showRemainingParamsDialog`, a generic `AlertWindow`
   with plain text editors and no validation. SongMappingsPane skips
   remaining-args entirely and hard-codes `args = [firstArg]` — multi-
   param actions silently lose every arg after the first.

3. **No concept of orphaned references.** When a track is deleted the
   action events on the action track that reference it are left with
   a stale UUID. Next trigger → crash. Same for bindings that
   reference the track, and for morphToPreset events whose preset
   belonged to a track that is gone.

The fix is one refactor, not three. Tighten the schema grammar, unify
the form-generation code, and cascade-delete dependent instances (with
a confirmation) when the thing they reference goes away.

## Design — the model after

### Typed primitives

Five primitives, derived directly from the args the current built-in
actions actually need. `string` is removed entirely — in today's
schemas it was always one of these in disguise.

```cpp
enum class ParamType {
    ChannelRef,  // UUID of a track / bus / "Main" (master)
    PresetRef,   // UUID of a preset, app-wide
    Enum,        // one of a listed set of values
    Float,       // number with optional min/max/default
    Morph,       // compound sub-action blob (escape hatch)
};

struct ParamSchema {
    std::string name;
    ParamType   type;
    bool        required = true;
    std::string defaultValue;                        // type-interpreted

    // Type-specific fields — unused fields ignored for that type.
    std::vector<std::string> scope;                  // ChannelRef: ["track","bus","master"]
    std::vector<std::string> sourceTypes;            // ChannelRef track scope: ["Instrument","AudioInput","Action"]
    std::vector<std::string> enumValues;             // Enum
    std::optional<double>    minValue;               // Float
    std::optional<double>    maxValue;               // Float
};
```

`ActionInfo.paramSchema: std::string` becomes
`ActionInfo.params: std::vector<ParamSchema>`. Built-in registration
moves from raw JSON literals to a typed builder. Persistence round-trips
the typed schema via JSON (for user-defined LLM actions). The stored
format is still JSON; the grammar is what changes.

### What the built-in schemas become

Concretely, translating the current eight built-ins:

| Action            | Today (paramSchema)                                                                                      | After                                                                                                                                                                    |
|-------------------|----------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `setActiveTrack`  | `[{trackName:string}]`                                                                                   | `[channelRef scope:[track] sourceTypes:[Instrument]]`                                                                                                                    |
| `fadeOut`         | `[{trackName:string},{duration:float},{easing:string}]`                                                  | `[channelRef scope:[track]], [float min:0 default:3], [enum values:[linear,easein,easeout,cosine,scurve] default:easein]`                                                |
| `fadeIn`          | same as fadeOut                                                                                          | same                                                                                                                                                                     |
| `crossfade`       | `[{fromTrack:string},{toTrack:string},{duration:float},{easing:string}]`                                 | `[channelRef scope:[track]] ×2, [float min:0 default:3], [enum easing]`                                                                                                  |
| `trackVolume`     | `[{channel:channel}]`                                                                                    | `[channelRef]` (no scope filter — any channel)                                                                                                                           |
| `morphToPreset`   | `[{trackName:string},{presetName:string},{duration:float},{easing:string}]`                              | `[channelRef scope:[track] sourceTypes:[Instrument]], [presetRef], [float], [enum easing]`                                                                               |
| `morphChain`      | six params, three refs                                                                                   | same shape, typed                                                                                                                                                        |
| `morph`           | `[{mode:morph}]`                                                                                         | `[morph]` (unchanged — special editor)                                                                                                                                   |

Note what *doesn't* need to exist:
- No `trackRef` as a separate type — it's `channelRef scope:[track]`.
- No `duration` type — `float` with min/default carries the same info.
- No `easingEnum` type — `enum` with fixed values does.
- No free-text `string` type — nothing in the current surface needs it.

When a future action needs a primitive we don't have, we add it then.

### The form generator

One class, `ActionInstanceForm` (likely in `src/gui/ActionInstanceForm.*`),
consumed by all three existing call sites:

```cpp
class ActionInstanceForm : public juce::Component {
public:
    ActionInstanceForm(StateAPI& state, const ActionInfo& action);
    void setInitialArgs(const juce::var& args);    // for editing existing instance
    juce::var  getArgs() const;                    // typed, validated, or null if !valid()
    bool       valid() const;                      // all required satisfied + type-checked

    std::function<void()> onAccept;
    std::function<void()> onCancel;
};
```

Renders each schema param with the right widget:

- `channelRef` → dropdown of matching entities (filtered by `scope` and
  `sourceTypes`). Shows "(no compatible channels)" and disables OK when
  the dropdown would be empty.
- `presetRef` → grouped dropdown: plugin → preset. "(no presets)" state
  as above.
- `enum` → dropdown of values, `default` pre-selected.
- `float` → numeric text field with min/max bounds. Non-numeric input
  and out-of-range highlighted in `statusError`.
- `morph` → button that launches `MorphEditor`; stores the returned
  compound blob as that arg's value.

OK button is enabled iff `valid()`. On accept, the form emits
structured JSON (the same format `ActionEventData.argsJson` and
`BindingState.args` use now, but guaranteed well-formed).

### Env-availability filtering

Currently the "don't offer actions that can't be satisfied" logic lives
only in `ProducePane::showActionPicker` and only filters the first arg.
This becomes a property of the schema evaluated centrally:

```cpp
bool actionCanInstantiate(const ActionInfo& a, const StateAPI& state);
```

True when every required param has at least one valid candidate in the
current state. Every picker (ProducePane's action menu, SongMappingsPane's
binding action menu, MorphEditor's sub-action menu) filters through this.

### Validation stance — fail hard

No defense-in-depth early-returns in action handlers. The assertion in
`StateAPI::track()` stays load-bearing. The form guarantees well-formed
args at creation time; cascade-delete (next section) guarantees refs
stay valid across entity deletion. If we still hit an assertion it's a
bug — the form missed a case, the cascade missed a path, or state got
corrupted. We want to find that, not swallow it.

### Cascade-delete with confirmation

Deleting an entity that has action instances pointing at it walks the
song first, collects references, and shows a single confirmation:

> Deleting **"Keys"** will also remove:
> - 2 action events on the action track
> - 3 MIDI bindings
>
> [Cancel] [Delete]

On confirm, the dependents are removed in the same transaction as the
primary delete. Cancel aborts the whole thing.

This matters especially during performance — accidentally deleting a
track that a foot pedal routes through would lose the routing silently
today. The confirmation makes it a deliberate act. For tracks with no
dependents, no dialog — proceed immediately.

Scope of the scan:
- Delete **track** → `ActionEventData` on the song's action track whose
  args contain this trackId; `BindingState.args` containing this trackId;
  `SendState` from/to this track.
- Delete **bus** → bindings referencing this bus; sends routed to it.
- Delete **preset** → action events whose args reference this presetId.
- Delete **song** → already cascades (whole song tree goes).

Reference-walking uses the new typed schema to know which arg slots are
refs — no string-matching.

### Persistence

Schema round-trip: typed `ParamSchema` serializes back to JSON for
storage in the actions table. The format is richer than today but
backwards-ignorable fields are additive. Since the project policy is
"no migration shims" at this stage, we rewrite the serializer/parser
in place; any existing `state.db` with old-format schemas is reset.

User-defined (LLM-created) actions persist the same typed schema JSON.
Lua's `createAction(name, label, luaCode, paramSchemaJson)` accepts the
new format. A short JSON grammar reference goes in `runtime/SYSTEM_PROMPT.md`
so the embedded Claude generates schemas the form can render.

## Build sequence

Each step a commit; app builds + tests pass at every step. Roll back
rather than patch-forward.

**Status as of 2026-04-20**: all steps landed on branch
`action-instances-refactor`. Ready to merge to main.

1. **Typed schema in state model.** Introduce `ParamSchema` +
   `ActionInfo.params`. Keep `paramSchema` as a deprecated fallback
   temporarily while call sites migrate. Rewrite the eight built-in
   registrations to use the typed builder. Tests for round-trip.
2. **Drop the JSON string `paramSchema` field.** Migrate remaining
   readers (UI, Lua, persistence serializer). `state.db` reset.
3. **`ActionInstanceForm` component.** Build in isolation; unit-test by
   constructing it with each built-in action's schema and verifying the
   rendered widgets + validation rules.
4. **Swap ProducePane's action-event dialog** to use the form.
   `showActionPicker` + `showRemainingParamsDialog` deleted from
   ProducePane/MorphEditor. MorphEditor's sub-action flow uses the form.
5. **Swap SongMappingsPane's binding form** to use `ActionInstanceForm`.
   Multi-param actions now work as bindings — no more silent arg drop.
6. **`actionCanInstantiate` + picker filtering.** Applied in all three
   pickers uniformly.
7. **Cascade scan + confirmation dialog.** Hook into track/bus/preset
   delete paths. Scan → confirm → delete-with-dependents as one commit.
8. **Docs.** Update `runtime/SYSTEM_PROMPT.md` with the new schema grammar so
   the embedded Claude generates valid schemas for user-defined actions.

## Out of scope

- Adding new action types or new param primitives beyond the five listed.
  If we need `int`, `songRef`, `regionRef`, free-text `string` later, we
  add them when an action actually requires them.
- A visual action-graph editor, drag-reordering of params, or any
  param-level inline editing in the timeline.
- Retroactive cleanup of existing songs that already contain bindings
  with stale refs — those are caught by load-time warnings (next point)
  or by the user re-saving after the form validates them.

## Decisions (resolved 2026-04-19)

- **Load-time validation**: on song load, walk bindings/events and show
  a one-time repair dialog listing any instances with unresolvable refs
  — user chooses to fix (re-select via form) or delete. Don't prune
  silently; don't defer to trigger-time. Matches the cascade-delete
  confirmation model and gives the user a chance to repair rather than
  lose work.
- **Preset scoping on edit**: when the `channelRef` param of a
  `morphToPreset` / `morphChain` is changed to a track with a different
  plugin, the `presetRef` field invalidates and highlights for
  re-selection.
- **Timing**: lands **before** 0.1.0. The underlying crash is
  reproducible with existing bad data, so every tester could hit it.
  Larger scope than originally planned for the beta round, but the
  alternative is shipping a known crash class.
