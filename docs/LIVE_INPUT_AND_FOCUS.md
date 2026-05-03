# Live MIDI input, track focus, and looper routing

**Status (2026-05-03):** Phases 1–5 shipped. Phase 6 (Boss-style looper) is functionally complete for both MIDI and audio — see status block at the top of `LIVE_LOOPING.md` for the per-piece breakdown. The doc below is kept as the design rationale; the "Phased implementation plan" section now reads as history rather than a forward plan.

This redesigns how live MIDI reaches instrument plugins, introduces a singular "focused track" concept, and reshapes the looper around those primitives. It supersedes the per-track record button UI in the Looper pane and fixes the current "all plugins receive all live MIDI" behavior that's both musically wrong (hitting a key plays every loaded instrument) and a CPU waste (every plugin running voice allocation on every event).

## Motivation

Three problems to solve at once:

1. **Every plugin sounds on every note.** Today, live MIDI is broadcast to all loaded instrument plugins via the graph's `midiInputNode`. Play a key → every loaded instrument sounds. For a looper building up track-by-track, that's a show-stopper: the performer can't selectively play one part at a time without manually muting everything else.

2. **CPU wasted on idle plugins.** Even when a plugin's output is muted, if it's receiving MIDI events it's still allocating voices and running synthesis. For a project with many loaded instruments, the cost is real and grows linearly.

3. **No notion of "the track I'm playing into."** We have a multi-select `selectedTrackIds` list for grouped UI actions, but no singular "cursor" at the track level — the single-click-to-focus gesture every DAW has. Without it, there's no natural place for "play here, record here" to attach.

## State contract

Three independent per-track axes, plus one app-level pointer. Each is meaningful on its own:

| Field | Type | Scope | Controls |
|---|---|---|---|
| `inputMonitoring` (`I`) | bool, per-track | persisted | Does this track receive live MIDI / live audio? |
| `armed` (`R`) | bool, per-track | persisted | Does this track capture during record mode? |
| `muted` | bool, per-track | runtime | Does this track's output reach the master? |
| `focusedTrackId` | singular TrackId | persisted on `SongState` | Which track is the "primary target" (UI cursor). |

`selectedTrackIds` (plural) also stays as-is — a separate concern, for grouped UI actions. Not conflated with `focusedTrackId`.

**Independence rule:** any combination of `I` and `R` on a track is a valid state. In particular, `R=on, I=off` is allowed — a "silent capture" where events are recorded into the track's take but don't reach the plugin. That requires **the capture tap to be upstream of the routing gate** in the signal path. Non-negotiable.

**Focus and `I`.** Focus is a UI concept; `I` is the engine gate. They're independent state fields, but the click semantics couple them conveniently (see below). If a user toggles `I` off on their currently-focused track, that's legal — they've chosen to be silent while still pointing the UI at that track.

## Click semantics (current GUI policy)

This is a policy of the GUI layer — *not* a state-layer invariant. A future GUI could surface focus, selection, and `I` however it wants. Today's rule:

| Click type | `focusedTrackId` | `selectedTrackIds` | `I` |
|---|---|---|---|
| Plain click on T | T | [T] | T→on, all others→off |
| Cmd-click on T | unchanged | toggle T's membership | unchanged |
| Shift-click on T | unchanged | range-select (existing) | unchanged |

So plain click is the Logic-style "make this one mine" gesture: focus, reset selection, snap MIDI routing to just this track. Cmd/Shift don't touch focus or routing — they only adjust the multi-select set for group actions. This preserves the existing Produce pane multi-select behavior untouched.

To play into more than one instrument at once, the performer manually toggles `I` on additional tracks (via the pill). To record into multiple simultaneously, they also toggle `R` on those tracks. Both pills remain per-track, fully independent.

## Signal path (engine)

```
Live MIDI in  →  per-event fanout in GraphWrapper::processBlock:
                   → for each track where armed: push to capture FIFO (tagged with trackId)
                   → for each track where inputMonitoring: schedule on that track's MidiSourceNode
                        (which feeds the plugin)

[No direct graph connection from midiInputNode to any plugin.]
```

Capture is upstream of routing. That preserves the `R=on, I=off` silent-capture case.

**Routing path reuses an existing abstraction.** `MidiSourceNode` already feeds each track's plugin for the arrangement/loop sequencer scan — we use `scheduleSingleMessage` per-track. Live input just becomes a second source of events flowing through the same per-track dispatch. One new loop in `processBlock`, no new node types, no dynamic graph wiring.

**Per-track atomics.** `MidiSourceNode` gains two `std::atomic<bool>` fields: `armed` and `inputMonitoring`. Message thread sets them via `EngineSync` in response to state events. Audio thread reads atomically per-event. Same pattern as the existing `recording` atomic on `GraphWrapper`.

**Note flush on `I` transition true→false.** When a track stops receiving live MIDI, send all-notes-off to its plugin so nothing hangs. Existing `flushAllNotes` infrastructure, new per-track variant.

**Broadcast wiring goes away.** `rebuildConnections` and track-add paths must stop wiring `midiInputNode` to any plugin. All MIDI to plugins flows through `MidiSourceNode`.

**Capture FIFO payload.** `RecordedMidiEvent` gains a `trackId` field. `drainRecordFIFO` dispatches pops to per-track takes by reading the tag. No per-track FIFOs; one queue, tagged entries.

## Looper changes that fall out of this

(All shipped by phase 6; described here as the rationale for the model that's now in code.)

Once per-track `I`/`R` and focused-track are in place, the Looper pane's per-track record button becomes the wrong abstraction. The model in code today:

- **Loop recording is a per-focused-track action.** Replace and overdub gestures (in the top bar, bindable via each cell's trigger slot) target the currently-focused track — no per-track record button. Boss-RC flow: focus → tap → play → second tap sets cycle length → focus next → tap.
- **Arming via `R` pills was dropped from the Looper.** R-arming machinery is gone from the Looper specifically (commit 844ed85); the Looper uses focus + gestures, not the armed set. Producer/Mixer keep `R` for arrangement recording.
- **The per-track `LoopRecordState` (`Off → Armed → Recording → StopPending → Off`)** was torn out in favor of a session-level `LoopAction` enum (`None / ReplaceQueued / OverdubQueued / CapturingReplace / CapturingOverdub`) tracked per-region. Several hundred lines of looper-specific code collapsed.

## Loop length definition (Boss-style)

Covered in `LIVE_LOOPING.md`'s updates. Summary: loop length is tap-to-tap on the first recording of the session. `cycleEnd` starts at 0 ("no cycle yet"); the second tap sets it to elapsed beats. Subsequent recordings align to that established cycle. See the looper doc for the full state machine; this routing/focus doc is upstream of it.

## GUI changes summary

(Reflects what shipped — see `TrackUi`, `LooperPane`, `BindableButton` in `src/gui/`.)

1. **Instrument tracks have the `I` pill.** Same visual as audio's `I` pill. Three pills on instruments in the Looper (`M S I` — `R` is gone there); four on instruments in Producer/Mixer (`M S R I`).
2. **Focused-track highlight** is a thickened left-edge type-color stripe on the track row, plus a row-bg shift via `TrackUi::rowBgToken(muted, focused, selected)` — applied uniformly across Produce, Mixer, and Looper.
3. **Click dispatcher** lives in `TrackUi::handleTrackClick(state, trackId, mods)`; Produce, Looper, and Mixer all call it.
4. **Looper pane** has no per-track record button; gesture buttons (replace / overdub / undo / redo / mute / clear / reset) live in the top bar's segmented `BindableButton` strip and act on the focused track. Each cell carries a per-button trigger-slot affordance for binding to a MIDI control.
5. **Transport bar global record.** The `r` key + transport record handle arrangement recording. Looper recording goes through the gesture buttons (which fire actions, which can themselves be MIDI-bound via the trigger slots).

## Phased implementation plan (history)

All phases below shipped. Kept here for traceability — each phase's section describes what landed, in roughly the order it landed. Phase 6 detail lives in `LIVE_LOOPING.md`.

### Phase 1 — State model: focus and per-track inputMonitoring for instruments

- Add `SongState::focusedTrackId`.
- `TrackState::inputMonitoring` already exists — document that it now applies to instruments too. Default for newly-created instrument tracks is TBD (see Open Questions).
- Add StateAPI: `setFocusedTrackId(TrackId)`, `getFocusedTrackId()`, emit a new `StateEvent::Focus` or reuse `Selection` (TBD).
- No engine behavior change yet. GUI can display but engine still broadcasts.
- Persistence: `focusedTrackId` persists as a config entry (same pattern as `app_mode`).
- Tests: state round-trip, idempotent setter.

### Phase 2 — Click rules + focus highlight

- Plain/Cmd/Shift click policy implemented as a shared helper.
- Wire Produce pane track-header clicks through it.
- Focused-track visual affordance.
- Mixer and Looper pane adopt the same helper.
- At this point, plain click updates `focusedTrackId` and snaps `inputMonitoring` — but engine still broadcasts, so the user doesn't yet experience the behavior change.

### Phase 3 — Engine: per-track MIDI routing

- `MidiSourceNode` gains `armed` / `inputMonitoring` atomics.
- `EngineSync` updates them on state changes.
- `GraphWrapper::processBlock` adds the live-MIDI fanout loop.
- `rebuildConnections` stops wiring broadcast `midiInputNode → plugin`.
- Note flush on `I=false` transition.
- This is the phase with real behavior change for existing users: live input stops being everywhere-at-once. Needs careful click-testing.

### Phase 4 — Capture FIFO: per-track

~~`RecordedMidiEvent` gains a `trackId` tag; drain dispatches per-track.~~
**Not needed.** When phase 3b landed, the existing `Arrangement::recordingTakes`
list already appended events to every armed track's take independently,
which gives multi-track capture for free. And capture is upstream of the
routing gate in `GraphWrapper::processBlock`, so `R=on, I=off` silent
capture already works. Skipping this phase.

### Phase 5 — Looper session-level record (shipped)

- `LoopRecordState` per-track machine torn out (commit 5f14ee7).
- Per-track record button removed from Looper pane (commit 844ed85, GUI cleanup).
- Replaced by session-level `LoopAction` enum (`None / ReplaceQueued / OverdubQueued / CapturingReplace / CapturingOverdub`) per-region.

### Phase 6 — Looper: Boss-style loop length + gesture top bar (shipped)

- Boss-RC tap-to-start, tap-to-stop bootstrap (commit 7e65b15).
- Queue-then-record-on-wrap for established cycle (commit 9d27209).
- Audio recording + playback wired (commit e62a322); real overdub via WAV sum-mix (commit 5536135).
- Top bar consolidated to a single segmented `BindableButton` strip with per-button trigger-slot binding (commits 980c6f1 → 97a195f → 1fd2f0a → 24c68fd).

See `LIVE_LOOPING.md` for the data model and capture/commit lifecycle.

## Known bugs (surfaced during phase 5a click-testing)

- **Stuck notes when stopping playback mid-note.** If the performer holds
  a key (or a recorded loop sustains a note across the transport-stop),
  the note hangs after the transport stops. `stopPlayback` sets
  `needsNoteFlush` and the next processBlock walks `activeNotes` to send
  per-channel noteOffs via every `MidiSourceNode` plus into the live
  midi buffer, so in theory the held note should be released. Needs
  investigation — possibly a race between the flush and the processBlock
  that immediately follows, or a subset of notes (e.g. live-input-only)
  isn't making it into `activeNotes`. Blocker for ship; not fixed yet.

## Open questions

- **Default `I` on new instrument tracks.** Options:
  - **On** — preserves current behavior (all tracks sound). Safer for casual sketching, but adds a manual-click tax for the looper workflow (turn off all but one).
  - **Off** — quieter-by-default, matches the "deliberate performer" mental model. Users have to explicitly enable a track to hear it. Unsurprising for looping; possibly surprising for casual use.
  - **On for first track in song, off for subsequent** — pragmatic. Common casual case ("load a plugin, play") works with no clicks; adding tracks doesn't flood.

  Leaning toward "off for new instrument tracks except the first" but deferring the call until GUI phase is in and we can click-test both.

- **Focus visual affordance.** Left-edge colored stripe vs name color vs row outline vs something else. Pick during phase 2.

- **StateEvent for focus changes.** Add `StateEvent::Focus` (new entity type) or reuse `Selection Updated`. New type is cleaner; `Selection` is already overloaded for UI redraws. Likely new type.

- **`focusTrack(id)` compound action.** Pedal-friendly wrapper that sets focus + resets `I` + arms. Either a Lua convenience or a new StateAPI method. Cheap to add. Phase 5 candidate.

- **Naming.** `inputMonitoring` is long and was coined when it only applied to audio. Candidates: `listen`, `liveInput`, `monitor`. UI is the one-letter `I` pill regardless. Keep the internal name or rename — taste call. Decide during phase 3.

- **What happens to selection when it's empty and user clicks a track?** Same as plain click: focus and select it. The existing ProducePane already does this; the new click handler formalizes it.

- **Focus persistence across song switches.** Focus is per-song. Switching songs loads that song's focused track. Matches existing per-song selection semantics.

- **Audio track `I` semantics.** Unchanged from today. Audio tracks still have independent `I` controlling "pass audio-in to out." They don't participate in focus routing (audio comes from physical channels, not a routing choice).

- **Multi-track MIDI-recording edge case.** Multiple `I+R` on tracks simultaneously → each records the same live MIDI. Free with this design. No special "doubling mode" needed.

## What this doc does not cover

- **Pane-mode-model** — `docs/PANE_MODE_MODEL.md` — where `AppMode` lives and how the GUI reconciles. Orthogonal.
- **Loop length definition** — `docs/LIVE_LOOPING.md` — Boss-style tap-to-tap and looper state machine post-refactor.
- **Grouped track actions** — `selectedTrackIds` multi-select for bulk fader / mute / etc. Not implementing now; concept is reserved.
