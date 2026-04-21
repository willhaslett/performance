# Pane mode model — Left slot as the workspace mode picker

**Status:** decision taken 2026-04-21 during live-looping click-testing; implementation is the immediate next step. Long-term refactor deferred to 0.2.x.

## Problem

Picking a pane in the sidebar should pick the workspace. Having to *also* flip a secondary toggle (the Looper Mode pill) to match is redundant and a reliable source of confusion — we saw it in testing: clicking "Looper" made the pane appear but the engine kept playing the arrangement, because `looperModeActive` hadn't been flipped.

The Left slot (Produce / Looper / Perform) already is the workspace-mode picker. Produce = "I'm arranging," Looper = "I'm live-looping," Perform = "I'm editing mappings." The `looperModeActive` flag has to reflect that, not live independently.

## State contract

`AppMode` is a typed enum stored on `AppState` — not `SongState`. It's workflow/session state, not song content. Per-song mode memory is a possible later feature; for now the app stays in whatever mode the user picked, across song switches.

```cpp
enum class AppMode { Arrangement, Looper };
```

What the mode means (the only thing any consumer is allowed to assume):

| `AppMode`           | Engine playback |
|---------------------|-----------------|
| `Arrangement`       | from `track.regions` |
| `Looper`            | from `track.loops`, cycle forced on |

Produce and Looper regions are mutually exclusive at runtime — the user can't hear arrangement content and loops simultaneously. The only connection between the two pools is at design time: the user copies regions between them (Phase 5).

## Current GUI policy (not a state-layer rule)

The current GUI — `MainLayout` with Left-slot panes — reflects mode by keeping the Left-slot pane content in sync with `currentMode`:

| Left slot contains | Mirrors to              |
|--------------------|-------------------------|
| Looper pane        | `AppMode::Looper`       |
| Produce pane       | `AppMode::Arrangement`  |
| Perform pane       | `AppMode::Arrangement`  |
| Hidden             | `AppMode::Arrangement`  |

This bidirectional mirroring is a policy of the *current GUI design*, implemented entirely inside `MainLayout` (the bridge in `setPaneContent` and the subscriber reading mode events). A future GUI — a toolbar picker, a command palette, a mode overlay, no GUI at all — could reflect the same state any way it wants. The state layer makes no assumption about how mode gets visualized or mutated from the GUI.

What the state layer *does* assume: mode is mutated through `StateAPI::setMode`, and emits an event on change. Anything consuming that event (engine, GUI, scripting) works the same way regardless of which GUI is on top.

## GUI implementation

All Left-slot changes go through one helper: `MainLayout::setPaneContent`. When the slot being changed is Left, the helper *also* calls `state.setMode(content == Looper ? AppMode::Looper : AppMode::Arrangement)` as part of the same operation. Every GUI path that can change the Left slot — sidebar click, `r`-key routing, keyboard shortcuts, future drag-and-drop — converges on this helper. One bridge point, no duplicate logic.

## Non-GUI clients

Lua, Claude via `perf`, IPC, MIDI bindings all call `setMode` on `StateAPI` directly. `MainLayout` subscribes to the mode-change event: when the mode flips from outside the GUI, the Left slot updates to match.

- `Looper` → Left = Looper pane
- `Arrangement` → Left = Produce pane (the arrangement-context default)

Recursion guard: `setMode` is idempotent — when the value hasn't changed, it doesn't emit an event. This prevents the setter-event-setter-event loop.

## Cycle coupling

`setMode(Looper)` forces `cycleEnabled=true` and `cycleStart=0` on the current song, so the loop-playback engine has the wrap boundaries it needs. Symmetrically, `setMode(Arrangement)` resets `cycleEnabled=false`. Looper mode owns cycle state while it's active; when mode leaves, cycle state is clean. Users who want arrangement cycle for non-looper playback use the transport cycle button in Produce — that's independent of mode.

## "Looper Mode" pill

Remove. Any GUI surface for mode is the GUI's choice; the state contract doesn't require one.

---

## Long-term refactor (deferred, 0.2.x territory)

**Important domain boundary:** UI layout (pane visibility, sidebar width, splits) is *not* song content. It's session preference. It belongs at the `AppState` level, never inside `SongState`. Loading a different song shouldn't move your mixer or swap your panes around — that would tangle "what I'm working on" with "how I like to see it."

### What's in scope for the refactor

Formalize UI layout state at the `AppState` level — the place where non-song, non-per-session preferences already live (device selection, theme, plugin catalog). Give it typed fields instead of the generic `config` key-value dict:

```
AppState::uiLayout = {
    leftPane, rightPane, bottomPane, sidebarPane : PaneContent
    sidebarWidth : int
    // future: divider positions, zoom levels
}
```

With that in place:

- `MainLayout::paneAssignments` goes away. Reads become `state.getUiLayout().leftPane`; writes become `state.setUiLayout(...)`.
- The `state.setConfig("pane_left", …)` string keys get replaced with typed accessors.
- The GUI still *observes* state changes via the event bus — the bridge between UI-layout state and the flag is just a narrower policy in `MainLayout`, not ownership.

### Domain separation summary

| State container     | What lives there                                                    |
|---------------------|---------------------------------------------------------------------|
| `SongState`         | track content, regions, loops, takes, cycle markers, bindings, tempo |
| `AppState`          | `currentMode`, device config, plugin catalog, (future) UI layout    |
| `MainLayout` transient | current pane assignments, read-through to AppState today         |

`currentMode` is explicitly **not** in `SongState` — it's a workflow/session choice that spans songs, not content of any one song. See the "State contract" section above.

### What this refactor does *not* do

- It does not unify song-state with UI-state. It keeps them in separate containers (`SongState` vs `AppState::uiLayout`), because they have different lifetimes and different ownership semantics.
- It does not couple the state layer to GUI concepts. The `PaneContent` enum that ends up in state is a pure data type — names the entries, carries no behavior. A headless client reads/writes the fields without needing `juce::Component`.

### Why defer

- The current wiring gives correct user-visible behavior now. This is a cleanup, not a correctness fix.
- The persistence layer and tests need updates. Non-trivial scope.
- Reasonable to ship 0.1.0 with MainLayout-owned pane state, then tidy post-beta.

### What the refactor gives us

- Non-GUI clients (Lua, Claude, IPC) can manipulate pane visibility via the same API path as any other state mutation — useful for scripted demos, automated testing, and future headless modes.
- Undo/redo of pane switches works naturally via the undo stack, for free.
- One SSOT for UI layout instead of the current triple (`paneAssignments` + `config` string keys + `savePaneConfig()`).
