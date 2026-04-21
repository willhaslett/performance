# Live looping — investigation

**Status:** investigation only; no design decisions yet.
**Date started:** 2026-04-20.

The user's phrasing in the opening prompt — *"2d matrix of squares, n tracks, n' tracks, dead simple affordances for writing over a cell, muting a cell, deleting a cell, same at the track level"* — is an accurate sketch of the dominant model in the field. This doc explains the landscape for context, then gets opinionated about what to build.

---

## What live looping is, in the field

Live looping is **capturing musical material in real time into fixed-length loops, then launching / layering / muting / arranging those loops from a control surface while you continue to play.** The performer is simultaneously composer, arranger, and player. Every affordance has to work hands-free or near-hands-free because the musician's hands and eyes are busy making music.

Two archetypes dominate:

### 1. The **clip-launcher matrix** (Ableton Live Session View, Bitwig Clip Launcher, Maschine Scenes)

A 2D grid. **Columns are tracks**, **rows are scenes** (musical sections — "verse," "chorus," "bridge"). Each cell in the grid holds a **clip**: one musical fragment with a fixed length in beats, attached to that track + that scene.

Launching a clip: you tap it, and it starts at the **next quantize boundary** (typically the next bar) so clips that start at slightly different wall-clock moments still line up rhythmically. A clip plays until it's stopped, relaunched, or replaced by another clip on the same track (only one clip per track plays at a time — the track is monophonic in the clip sense, like channels in a stripper DAW).

Launching a **scene**: fires every clip in that row simultaneously. This is the "song form" surface — scenes are the arrangement.

This is what the user's sketch describes. Ableton + Push/Launchpad is the de facto laptop-performer rig.

### 2. The **tape-style looper** (Boss RC-505, Ditto, Headrush Looperboard, Infinite Jukebox)

Linear, textural, additive. You press record, play a phrase, press record again to close the loop — the first loop's length becomes the master loop length. You then **overdub** layer after layer on top of the same master loop, building a denser soundscape. Usually 4–8 parallel "tracks" max (more is hard to control without a big grid).

Emphasis is on evolving textures in one sitting, less on switching sections. Great for solo textural performers (KT Tunstall, Ed Sheeran, Reggie Watts). Weaker for structured songs with choruses.

### What "a live looping rig" actually is in 2026

Almost always the matrix model. Tape-style loopers still ship as pedals for guitarists, but software-centric rigs use the grid because it scales to structured arrangements without losing the real-time-capture core. Our user asked for the matrix, and the matrix is the right default.

---

## Vocabulary the design should use

- **Cell** — one unit of musical content at a (track, scene) coordinate. Has state: empty / recording / playing / stopped. MIDI or audio.
- **Track** — column in the matrix. Usually one instrument or input. Monophonic *w.r.t. cells* — only one cell on a given track plays at a time.
- **Scene** — row in the matrix. Launching a scene launches all its cells.
- **Launch quantize** — global setting: when you tap a cell, when does it actually start? Typical values: off, 1/4 bar, 1/2 bar, 1 bar, 2 bars, 4 bars. **1 bar** is the universal default.
- **Count-in** — some systems add a metronomic count before a record pass. Others punch in at the next quantize boundary with no count.
- **Overdub** — record layer on top of an existing cell without erasing (Boss model). Ableton doesn't do this natively at the cell level.
- **Retrigger** — launching a cell that's already playing. Most systems default to "restart from beginning"; some offer "toggle stop" instead.
- **Panic / stop all** — kill every playing cell instantly (or at next bar, depending on config). A must-have for live.
- **Follow actions** — a clip can be configured to automatically launch another clip after N plays. Ableton's most underused feature. Out of scope for v1.

---

## Why this is a natural fit for Performance

Our current state model already has everything this feature needs, minus the matrix coordinate:

- **Tracks** — already exist. Cells hang off tracks.
- **Regions** — already the atom of "a chunk of musical content with a length, MIDI or audio." A cell in our system is **a region with launch metadata** instead of a timeline position.
- **Sequencer / transport / global beat clock** — already running. Launch quantize is just "wait until the next bar boundary according to the already-running clock."
- **MIDI / audio recording** — already works. We don't need new capture code; we need to point capture at a cell's region instead of a timeline position.
- **Actions + bindings infrastructure** — already the right shape for "bind MIDI pad 4 to `launchCell(1, 2)`." The action-algebra refactor makes adding new built-in actions cheap.
- **State event bus** — GUI refreshes automatically when regions change.

Where the fit isn't automatic: the **Produce pane** is timeline-centric. A 2D matrix wants a different view. And we need a data delta: regions today have a linear `startBeat`; cells are (trackIdx, sceneIdx) coordinates.

---

## Proposed architecture

### Data model — regions stay regions, gain a `kind`

```cpp
struct RegionState {
    // ... existing fields (id, type, startBeat, lengthBeats, takes...)

    enum Kind { Arrangement, Cell };   // NEW
    Kind kind = Arrangement;

    // NEW — only meaningful when kind == Cell:
    int sceneIndex = -1;               // row in the matrix
    // (trackIdx is implicit — the region already belongs to a track)
    bool loopWhenTriggered = true;     // false = one-shot
    bool isRecording = false;          // transient, set during capture
};
```

This keeps the two mental models (timeline arrangement vs. matrix session) in one data type, lets us share all existing persistence / undo / engine-sync plumbing, and lets a single track hold both — arrangement regions for sections that always play the same, cells for the live-triggerable material.

Alternative — a separate `CellState` parallel type — is cleaner conceptually but doubles the state surface and forces every feature (persistence, event bus, GUI rendering) to learn about two types. Not worth it.

### A new pane: "Session"

Add a fifth main pane alongside Produce / Perform / Chat / Mixer. Keyboard shortcut ⌘L (for "Loops") or similar. The Session pane renders the matrix:

- Columns = tracks (same tracks the rest of the app sees)
- Rows = scenes (new concept at the project level — just an integer count, effectively)
- Cells painted by state: empty / armed / recording (red pulse) / playing (green pulse) / stopped
- Click a cell to launch; Cmd+click to arm for record; right-click for delete / rename / duplicate
- A "scene launcher" column on the far right fires a whole row

Produce pane stays unchanged — arrangement regions still render there on the timeline. If a region is a Cell, Produce doesn't show it (it has no timeline position). If it's an Arrangement region, Session doesn't show it.

### New built-in actions (for MIDI bindings)

Every interactive affordance gets a bindable action so a musician can map it to their controller:

| Action | Arguments | Behavior |
|---|---|---|
| `launchCell` | `trackRef`, `scene` | Start/stop cell at this coordinate (quantized to launch-quantize) |
| `recordCell` | `trackRef`, `scene` | Arm the cell; record starts at next quantize boundary, length = launch-quantize × count or until pressed again |
| `stopTrack` | `trackRef` | Stop whatever cell is playing on this track |
| `stopAllCells` | — | Global panic (quantized or instant, depending on hold duration) |
| `launchScene` | `scene` | Fire every cell in this row |
| `scrollMatrix` | `dx`, `dy` | Move the visible 4×4 window when the matrix exceeds 4×4 |
| `setLaunchQuantize` | `quantizeValue` | Switch between 1/4, 1/2, 1, 2, 4 bar quantize |

With your KeyLab 88 MkII (16 pads + transport controls), a natural default mapping is:

- 16 pads → 4×4 visible window onto the matrix
- Arrow buttons (or two pads reserved) → `scrollMatrix`
- Record button → `recordCell` on the currently-selected cell
- Stop button → `stopAllCells`
- One of the pad-bank buttons → `launchScene` (held = scene-launch mode, release pad to fire row)

The prescribed MIDI map ships as the default for a KeyLab; we document it prominently and users with other controllers can remap via the existing SongMappingsPane.

### Launch-quantize engine

One new piece of engine code — a **launch scheduler**. Small:

- Keeps a list of pending `(action, fireAtBeat)` tuples.
- On every engine tick, fires any actions whose `fireAtBeat <= currentBeat`.
- `launchCell(track, scene)` computes `fireAtBeat = ceilToQuantize(currentBeat, quantize)` and enqueues.
- Same for scene launches and global stops.

The Sequencer already exposes a `beatCallback` we can hook; no new thread / clock work.

---

## Open design questions (for the conversation before coding)

1. **MIDI-only in v1, or MIDI + audio?**
   - MIDI cells: cheap. We already record MIDI into regions.
   - Audio cells: real-time audio capture + seamless loop playback is meaningfully harder (buffer management, sample-accurate loop boundaries, disk I/O during performance). Doable, not free.
   - Recommendation: **MIDI only for v1**, audio in v2. A musician recording loops through a synth (keys user) gets the full value with MIDI.

2. **Scenes for v1?**
   - Adds complexity (scene launch action, scene UI column, scene persistence).
   - Removing scenes means the matrix is really "tracks × positions" where each position is just a slot, with no row-level launch semantics.
   - Recommendation: **yes, include scenes**. They're core to the clip-launcher mental model; without them the matrix feels like a sampler grid (still useful, but less structural). The implementation cost is small — a scene is just an integer; launching one iterates cells with matching sceneIndex.

3. **Overdub in v1?**
   - Classic Boss feature. Adds meaningful complexity: record-while-cell-plays, layered takes, undo-last-layer.
   - Recommendation: **no for v1**. "Record replaces" is simpler and covers the clip-launcher idiom. Overdub is a tape-looper feature we can add later if testers miss it.

4. **Launch quantize default?**
   - Almost every system defaults to **1 bar**. Recommend the same.
   - User-configurable via `setLaunchQuantize` binding + a Session-pane menu.

5. **Cell length determination?**
   - Two models: fixed (set before recording — e.g. "record a 4-bar clip") or punch-out (press record to start, press again to stop, length = beats between).
   - Recommendation: **punch-out with bar-quantized boundaries**. Matches Ableton and most performers' muscle memory. First press records the next bar-boundary; second press ends at the next bar-boundary.

6. **Retrigger semantics when a cell is already playing?**
   - Two options: **restart from beginning** (Ableton default) or **toggle-stop**.
   - Recommendation: **restart from beginning**. Toggle-stop is what `stopTrack` is for.

7. **Does a cell persist its recorded content across quit/relaunch?**
   - Yes — they're regions, and regions persist. The only wrinkle: we need cell content in SQLite schema (already there via region state). Free.

8. **Cells vs arrangement — do they interact?**
   - If a track has both timeline-arrangement regions and matrix-cells, and both try to play at once, what wins?
   - Recommendation: **cell wins.** When a cell is playing on a track, the arrangement for that track pauses. Simpler than mixing two sources of MIDI into one track.

---

## MVP shape candidates

### A. Matrix-only, minimum viable
- 4×4 Session pane (scrollable if bigger)
- MIDI cells, no audio
- Scenes included
- Launch quantize = 1 bar fixed
- Punch-out recording
- Restart-from-beginning retrigger
- `launchCell` / `recordCell` / `stopTrack` / `stopAllCells` / `launchScene` / `scrollMatrix` bindable actions
- Default KeyLab 88 MkII mapping shipped
- No overdub, no follow actions, no audio cells

**Effort estimate:** 2–3 days. Scenes are cheap, the scheduler is cheap, the pane is the main cost.

### B. "Real" session view
- Everything in A, plus:
- Audio cells
- Overdub
- User-configurable launch quantize
- Scene column + scene launch affordances
- Per-cell color / label / launch mode overrides

Double the surface; probably a week. Some of this *might* ship for free once A is in (e.g. user-configurable quantize is just exposing the internal value).

### Recommendation

**Do A.** Ship. Watch testers live-loop for a couple of sessions. Add what they miss.

The biggest risk in this feature isn't "will it work" — the data model, engine work, and UI are straightforward given what we already have. The risk is **the MIDI mapping not feeling right under the hands of a musician who's also playing.** That only reveals itself in use. Shipping A quickly optimizes for feedback cycles on the ergonomic question.

---

## Recommended next step

Same shape as composer and bundled plugins:

1. Read the above. Push back / correct my assumptions about what a performer actually needs.
2. Answer the 8 open questions (or agree with my recommendations).
3. I scope A into phases and execute.

Two questions I'd especially like you to weigh in on up front:

- **The matrix dimensions question.** "N tracks × N' tracks" in your prompt is probably a typo. I'm assuming you meant **N tracks (columns) × M scenes (rows)**. Confirm, or correct me.
- **Does Session replace something or sit alongside?** Sidebar currently has Produce / Perform / Chat / Mixer. Adding Session as a fifth pane is natural. Alternative: a mode toggle on Produce (Timeline vs Session). Your call.
