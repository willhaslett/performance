# Live looping — investigation + design

**Status (2026-05-03):** the data model + cycle math + capture/commit lifecycle described here are in code and shipped. The **Looper pane GUI section below is stale** — the pane was redesigned twice since this doc was written (per-track record button → session-level gestures → segmented `BindableButton` strip with per-button trigger slots). For the current GUI, read `LooperPane.h`, `BindableButton.h`, and the "Current focus" block in `CLAUDE.md`. The "Out of scope" list at the bottom is also stale: **audio loops + audio overdub shipped** (commits e62a322, 5536135). See **Implementation status** at the bottom for the current rundown.

The investigation section surveys the field. The Design section below is what we're actually building — and it diverges from the "Ableton-style matrix" the investigation opens with. The deeper the investigation went, the clearer it became that the user's intuition about a single looping timeline with one region per track was *not* an under-specified matrix; it was a legitimately different model, and the right one for this app.

---

# Investigation

## What live looping is, in the field

Live looping is **capturing musical material in real time into fixed-length loops, then arranging those loops while you continue to play.** The performer is simultaneously composer, arranger, and player. Every affordance has to work hands-free or near-hands-free because the musician's hands and eyes are busy making music.

Two archetypes dominate in the field:

### 1. The clip-launcher matrix (Ableton Live, Bitwig, Maschine)

A 2D grid. Columns are tracks, rows are scenes (song sections). Each cell holds a clip — one musical fragment attached to that track + that scene. Tapping a clip starts it at the next quantize boundary. Tapping a scene fires every clip in that row. Only one clip per track plays at a time. Ableton + Push is the de facto laptop-performer rig.

### 2. The tape-style looper (Boss RC-505, Ditto, Headrush)

Linear, textural, additive. Press record, play a phrase, press record again to close the loop — the first phrase's length becomes the master length. Overdub layer after layer on top. Everything shares one master loop. Ed Sheeran, KT Tunstall, Reggie Watts.

Modern software rigs are almost always matrix-primary. Tape pedals are still common with solo guitarists.

## Why neither archetype is exactly what we built

The user's prompt described a 2D grid, and the doc initially proposed a matrix implementation. Through conversation, the actual model turned out to be **neither** archetype. Three specific divergences:

- **One region per track, not N cells.** A track has exactly one "loop region." Variations are stored as takes inside that region, not as sibling cells in a column.
- **No scenes, no cross-track coordination primitives.** Each track's loop plays or doesn't; there's no "fire this row of clips together" surface.
- **The cycle length is the only quantization.** No per-cell launch quantize (1/4 bar, 1/2 bar, etc.). Changes happen at the cycle wrap, full stop.

The result is closer to a multi-track loop sequencer (hardware grooveboxes, early Korg Electribes, Novation Circuit) than to Ableton Session View. Simpler, fewer primitives, and a better fit for what this app's user is actually trying to do.

---

# Design

## Shape in one sentence

**A Looper pane is an alternative view onto the same Project.** It replaces the Producer pane in the main content slot, cycles a user-set loop length continuously, and lets each track carry one loop region that either plays continuously within that cycle or repeats inside it (if shorter). No cells, no scenes, no matrix, no per-cell launch quantize — the cycle *is* the quantization.

## Data model

Per project:

```
Project
  cycleLengthBeats: int        // persisted; user-set via setCycleLength
  looperModeActive: bool       // persisted; determines pane + invariants
  // + existing cycle fields (loopStart / loopEnd / loopEnabled) —
  //   normalized to loopStart=0, loopEnabled=true when looperModeActive
```

Per track:

```
Track
  regions: vector<RegionState>   // existing — arrangement pool, used by Producer
  loops:   vector<RegionState>   // new — looper pool, used by Looper
  // + existing fields (muted, etc.)
```

**Two pools per track, genuinely independent.** Creating, deleting, editing, recording into, or swapping takes on one has zero effect on the other. Producer never sees loops; Looper never sees arrangement regions. This closes the foot-gun the user surfaced — "Producer delete accidentally nukes a loop you're about to trigger live" — and leaves room for the concurrent-capture workflow the user flagged as a future possibility (record the Looper's output via Producer while performing).

Invariants on entries in `loops`, enforced at the state API:

- `startBeat == 0` always
- `lengthBeats` can be any positive value — even longer than `project.cycleLengthBeats`. Tail is preserved; playback clips until the user extends the cycle.
- Existing `takes` + `activeTakeId` fields handle variant swapping with no new machinery.
- New `pendingTakeId` field on `RegionState` (empty when no swap pending) — see take-swap rule below.

Enforcement lives at the state API, not the GUI. Writes from any caller (GUI, Lua, IPC, MIDI binding) normalize the invariants when `looperModeActive` is true. This matters because the runtime has multiple non-GUI clients — chat's `perf` tool can write state directly.

## Playback rule

One formula governs everything:

```
regionPosition = cyclePosition mod region.lengthBeats
```

Where `cyclePosition = globalBeat mod project.cycleLengthBeats` (the sequencer already wraps this).

- **Region shorter than cycle** — plays multiple times per cycle. 4-bar loop in 16-bar cycle plays 4×.
- **Region equal to cycle** — plays once per cycle.
- **Region longer than cycle** — tail past `cycleLengthBeats` is never reached; data stays on disk. `setCycleLength(longer)` surfaces the tail.
- **Region between 1× and 2× cycle** — plays once fully, then wraps and partially plays again.

No truncation at capture, no special cases, no floor on cycle length derived from region content.

## Recording rule

- Punch-in on a track → record starts at the next cycle wrap.
- Captured events go into a new take on that track's loop region. Positions stored relative to punch-in (region-local beats).
- Punch-out → region `lengthBeats` = punch-out beat − punch-in beat.
- If the user records past the cycle boundary, the region grows past `cycleLengthBeats`. Playback clips per the formula; data preserved.
- Stuck-note hygiene: synthetic noteOffs injected at punch-out for any still-held notes (reuses the existing MIDI recording pattern).
- Recording creates a new take, promoted to active. Previous takes persist on the region.

## Take-swap semantics

User changes active take mid-performance → deferred to next cycle wrap. Implementation:

- `region.pendingTakeId` holds the requested take; `region.activeTakeId` is what's playing.
- User action writes `pendingTakeId`.
- On cycle wrap (sequencer's loopWrap callback), the playback engine:
  1. Sends noteOffs for any still-held notes from the current take.
  2. Sets `activeTakeId = pendingTakeId`; clears `pendingTakeId`.
  3. Begins the new take from its beat 0.

Clean entrance at a musical boundary; no mid-cycle cut.

## Cycle length semantics

- `cycleLengthBeats` is purely user-set via the new `setCycleLength(bars)` action (bindable to MIDI).
- Default on a new Looper-mode project: 16 bars (64 beats at 4/4).
- No floor constraint from regions — user can set any positive value.
- Recording never auto-grows cycle length. Oversized regions stored in full, playback clips.
- The underlying sequencer's cycle state (`loopStart`, `loopEnd`, `loopEnabled`) is the same state used by Producer's cycle-mode today. Looper-mode writes normalize: `loopStart=0`, `loopEnabled=true`, `loopEnd=cycleLengthBeats`.

## Movement and copy primitives

Five new Lua-bindable actions:

| Action | Purpose |
|---|---|
| `setCycleLength(bars)` | User-set cycle length. Takes effect immediately. |
| `moveRegion(regionId, targetTrackId)` | Within the arrangement pool, relocate to another track. Type-checked: MIDI↔MIDI, audio↔audio. |
| `moveLoop(loopId, targetTrackId)` | Symmetric within the loop pool. Same type check. |
| `copyArrangementToLoop(regionId)` | Edge-case: duplicate an arrangement region as a loop. New id, new takes. `startBeat` discarded. |
| `copyLoopToArrangement(loopId, startBeat)` | Duplicate a loop region into the arrangement at a given start beat. |

Cross-pool copy is an **edge-case primitive** — no menu entry in v1, no visible indicator. The bindings exist so it's not trap-doored; chat or a power user reaches it via Lua.

## Pane-aware keybindings

Currently our keybinding system is global. With Looper, some shortcuts need pane-aware routing — `c` (toggle cycle) is meaningful in Producer but a no-op in Looper. Small extension:

- Each registered command gains an optional `applicableWhen: PaneContext` filter.
- The dispatch layer reads the currently-visible main-slot pane and skips commands whose filter rules it out.
- Default `applicableWhen = Any`. Most commands don't need the filter.

## Sidebar / navigation

Looper occupies the same UI slot as Producer (the Left / main content slot). They're mutually exclusive; switching to Looper closes Producer. Sidebar gains a "Looper" entry next to "Produce":

```
View
  Produce   ⌘Y
  Looper    ⌘?        ← new, shares slot with Produce
  Perform   ⌘U
  Mixer     ⌘O
  Chat      ⌘I
```

Keyboard shortcut TBD — pick during implementation, after checking for conflicts.

## What's persisted vs. transient

**Persisted (SQLite):**
- `project.cycleLengthBeats`
- `project.looperModeActive`
- `track.loops` — full regions + takes + `activeTakeId`. Same persistence machinery as `regions`.

**Transient (runtime, resets on relaunch):**
- `region.pendingTakeId`
- Sequencer position, cycle count, etc. — already transient today.

---

# Phased plan

Five phases, each shippable on its own, building strictly upward.

## Phase 1 — State model

- Add `project.cycleLengthBeats`, `project.looperModeActive`, `track.loops`.
- Extend `RegionState` with `pendingTakeId`.
- Extend persistence schema (new table for loop regions, or a `pool` discriminator on the existing regions table).
- Tests: round-trip a project with loops; verify `loops` collection is independent of `regions`.

## Phase 2 — Playback engine

- Implement within-cycle region wrap: when `looperModeActive` is true, drive `track.loops[0]` (if present) using `regionPosition = cyclePosition mod region.lengthBeats`.
- Arrangement-pool playback unchanged.
- Tests: 4-bar loop in 16-bar cycle plays 4×; 20-bar loop in 16-bar cycle plays first 16 bars only; extend cycle to 24, first 20 bars of region play then wraps.

## Phase 3 — Recording + take-swap

- Punch-in / punch-out recording that creates takes on loop regions.
- `setCycleLength(bars)` action.
- Take-swap at cycle wrap (pendingTakeId → activeTakeId with noteOff flush).
- Tests: record a 3-bar loop stored as a single take; mid-cycle take change takes effect at wrap with clean note-offs.

## Phase 4 — Looper pane

- New pane component sharing the main content slot with Producer.
- Renders per-track loop tiles: take name, length, mute state, record button, take selector.
- Keyboard shortcut + Sidebar entry.
- Pane-aware keybinding infrastructure.
- Tests: GUI integration + manual verification.

## Phase 5 — Movement and copy primitives

- Expose `moveLoop`, `moveRegion`, `copyLoopToArrangement`, `copyArrangementToLoop` as Lua bindings.
- Right-click menu entries in each pane for the same-pool moves.
- Tests: type-check enforcement; copy produces fully independent region with new ids.

## Out of scope for v1

(Updated 2026-05-03 — this list shrank as the looper matured. Items struck through here shipped.)

- ~~Audio loops. MIDI only for this release.~~ **Shipped** — audio recording + playback wired (commit e62a322).
- ~~Overdub (add layer to an existing take). Not part of our model.~~ **Shipped** — both MIDI overdub and audio overdub (via WAV sum-mix) work; commit 5536135 for the audio path. Gesture lives in the top-bar `BindableButton` strip.
- Scenes / cross-track coordinated launch. Not applicable.
- "Start from arbitrary beat" when launching a take. The model is always beat-0-at-wrap.
- Follow actions, per-take quantize, per-loop tempo.

---

# Open questions deferred

- **Default keybinding letter for Looper pane.** Pick during Phase 4 after checking conflicts.
- **Pane-aware keybinding filter — implementation detail.** Global table with filter, or per-pane local tables with fallback. Decide during Phase 4.
- **Punch-in UX when transport is stopped.** Auto-start the transport (Ableton does)? Probably yes. Decide during Phase 3.
- **`moveLoop` / `moveRegion` same-pool semantics when the target track already has a loop.** Replace, refuse, or keep both (breaking "one loop per track")? Decide during Phase 5.
- ~~**User-facing UX for `looperModeActive`.**~~ Resolved 2026-04-21: the Left-slot pane content IS the mode picker. `looperModeActive == (leftSlot == Looper)`. Produce / Looper / Perform are mutually exclusive workspaces. See `docs/PANE_MODE_MODEL.md`.

---

# Implementation status (2026-05-03)

The data model + cycle math + capture/commit lifecycle are in code. The Looper pane has been substantially redesigned twice since the original Phase 4 GUI landed; what's there now does not match the old description.

**What's in code today:**

- **Data model** — `track.regions` (arrangement) and `track.loops` (looper) as fully independent collections; `RegionState` carries `pendingTakeId` and a `loopAction` enum (`None / ReplaceQueued / OverdubQueued / CapturingReplace / CapturingOverdub`). `cycleEnabled` + `cycleStart=0` enforced when `AppMode::Looper`.
- **Engine** — mode-gated dispatch in `GraphWrapper::processBlock` (both MIDI *and* audio file regions are silenced when the engine is in Looper mode, so arrangement content can't bleed in). `Arrangement::loopForTrack(TrackId)` returns the loop region (or null) for the looper code path.
- **Capture/commit** — Boss-RC: first tap on `replace` starts capturing, second tap defines cycle length and commits the take. Subsequent gestures queue and commit on the next cycle wrap. Replace truncates the WAV file before the writer opens; overdub writes to a `.overdub.wav` temp, then sum-mixes both into a `.mix` file and atomically renames on commit. In-flight content (notes + waveform) paints live during capture, capped at the live playhead.
- **GUI** — top bar is a single segmented `BindableButton` strip (play | up down | replace overdub | undo redo mute clear | reset, uniform 84px cells); each cell carries a per-button trigger-slot affordance for binding to a MIDI control (menu lists every registered control, disables already-bound, "Manage controls" jumps to Perform pane). Pane title "Looper" centered above the track-headers column. App-level toolbar removed; build info at the sidebar bottom.
- **Tests** — 252 in tree (`tests/PerformanceTests.cpp`). Looper-specific suites cover state transitions, capture commit length, the Boss-RC redux path, transport-stop-during-capture, queued-stop-cancels.

**Still open (ship blockers):**

- **Bootstrap-after-reset bug.** After clicking the in-app reset, neither Play nor Replace appear to do anything. Diagnosed as either a focused-track or transport-state half-clear; not yet root-caused.
- **Stuck notes on transport stop** — Looper-only, intermittent. `[LoopDump]` diagnostic logging in tree (commit `d017823`).
- **Audio latency** ~85ms one-way at 128 samples — much higher than expected. Snap-toggle landed for sample-accurate measurement.

**Deferred:**

- Phase 5 movement/copy primitives (`moveLoop`, `moveRegion`, `copyLoopToArrangement`, `copyArrangementToLoop`) — never landed; still considered nice-to-have rather than ship-blocker.
- Producer ↔ Looper tempo coupling (one song = one tempo; documented in CLAUDE.md backlog).

