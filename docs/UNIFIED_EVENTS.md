# Unified events + sample-accurate audio-thread tempo

**Status (2026-05-10):** active sub-sub-sub-project. On dev branch `composition-abc` (blocking the AI composition merge). No hurry — this is engine work that needed to be factored out of the composition push when sample-accurate tempo handling turned out to require a real refactor.

**Parent context:** the AI composition redesign (`docs/AI_COMPOSITION_API.md`) reached the point of needing tempo events that *audibly take effect* at the right beat. Phase 3c attempted to walk a tempo map on the audio thread but hooked into GraphWrapper's existing fragile beat-state machine and produced bug-after-bug. Will paused the work to ask whether this was complexity-shaped — it was. This doc captures the plan to land it properly.

## Why this exists

Two convergent realizations:

1. **Tempo changes need sample-accurate audio-thread handling.** Audio playback in our system doesn't time-stretch — files play at sample rate. The "tempo" affects WHEN audio regions start and when beat-locked events fire. A late tempo change shifts the start sample of a region, which produces audible misalignment for layered material. ~16ms of message-thread polling latency is acceptable for solo monophonic but unacceptable for layered audio. This pushes tempo evaluation onto the audio thread.

2. **Events ARE instances of actions.** Will's framing during the design conversation: a tempo change is an instance of a built-in action `setTempo` with payload `{bpm: 90}`, scheduled at a beat. Same shape as `fadeOut` with payload `{track, duration, easing}` scheduled at a beat. They differ only in *which action* they invoke and what args. Storage and API can be uniform; only dispatch needs to differentiate (most actions run on message thread; transport-affecting actions like setTempo / setTimeSignature run inline on the audio thread).

These two together: storage is uniform (one event-instance vector), Lua API is uniform (already is — `createEvent` etc. shipped in 3d-b), but the dispatch table flags `setTempo` / `setTimeSignature` as audio-thread-inline so the audio thread can evaluate them at sample resolution.

## Architecture

```
                    ┌──────────────────────────────────────┐
                    │         message thread               │
                    │                                       │
                    │  SongState.actionEvents (one vector) │
                    │            │                          │
                    │            ▼                          │
                    │  Lua / GUI mutate via createEvent     │
                    │            │                          │
                    │  ┌─────────┴────────────────┐         │
                    │  │ on Song::Updated event:   │         │
                    │  │   filter to tempo + ts    │         │
                    │  │   atomic_store snapshot   │         │
                    │  └─────────┬────────────────┘         │
                    │            │                          │
                    │  fire non-transport actions when      │
                    │  playhead crosses (existing path)     │
                    └─────────── │ ─────────────────────────┘
                                 │
            shared_ptr<vector<TempoEvent>> (lock-free swap)
                                 │
                                 ▼
                    ┌──────────────────────────────────────┐
                    │          audio thread                │
                    │                                       │
                    │  GraphWrapper::BeatState              │
                    │    (refactored — single owner of      │
                    │     baseBeat, samplesSinceStart,      │
                    │     tempo, tempoMap)                  │
                    │                                       │
                    │  per buffer:                          │
                    │    advance(numSamples) walks events   │
                    │    splits beat math into segments     │
                    │    where tempo crosses                │
                    │                                       │
                    │  MIDI scan + audio-region start uses  │
                    │  segment-aware beat→sample mapping    │
                    └──────────────────────────────────────┘
```

**Key constraint that rules the design:** the audio thread can't take a mutex and can't allocate. The tempo-map view is a `shared_ptr<const vector<TempoEvent>>` swapped via `atomic_store` from message thread, `atomic_load` from audio thread. Standard C++20 lock-free pattern.

**Sub-buffer accuracy without splitting `graph.processBlock`:** when a tempo event lands mid-buffer, we DON'T process the buffer in segments through the graph (that would re-invoke plugins multiple times — expensive, may break stateful plugins). Instead, we keep `graph.processBlock` as one call and make our beat-tracking math piecewise. MIDI events fire at sample offsets computed from segment-correct beat→sample mapping. Audio file regions start at sample-accurate offsets. The audio CONTENT continues playing at sample rate (no time-stretch).

## What changes vs. what stays

**Stays:**
- `createEvent` / `listEvents` / `getEvent` / `setEvent` / `deleteEvent` Lua API (already unified in 3d-b).
- The four type strings (`tempo` / `timesig` / `key` / `action`) in the LLM-facing API. Internally these all become action instances with different `actionId`.
- Existing built-in actions (`fadeOut`, `crossfade`, `morphToPreset`, etc.) — unchanged.
- Audio playback content path — no time-stretching. Audio plays at sample rate.

**Changes:**
- `SongState`: drop `tempoEvents` / `timeSigEvents` / `keyEvents` typed vectors. Everything lives in `actionEvents` (renamed → `events` for clarity).
- New built-in actions registered: `setTempo`, `setTimeSignature`, `setKey`. The first two flagged as audio-thread-inline.
- `GraphWrapper`: beat-state extracted into a `BeatState` class with a single `advance()` method. All mutations of baseBeat / samplesSinceStart / tempo go through it. State coupling is explicit and tested.
- Audio thread: walks tempo events per buffer; splits beat math at crossings; updates `BeatState.tempo` at the segment boundary.
- Persistence: tempo_events / timesig_events / key_events tables collapse into one events table. Migration: none — `bin/reset` handles it.

**Things deliberately deferred:**
- Audio time-stretching (huge separate project; not needed for the scenario Will named).
- Loop wrap × tempo change continuity (existing limitation, documented).
- Plugin `AudioPlayHead` PositionInfo (separate gap noticed earlier).
- Sub-buffer audio-region time-stretching for tempo curves over a region.

## Implementation phases

Each phase commit-landable independently; tests pass at every step.

### Phase E1 — Revert 3d-c, neutralize the broken state coupling

- Remove the tempo-map walking block I added to `GraphWrapper::processBlock`.
- Remove `tempoMap` field and `setTempoEvents` from GraphWrapper / AudioEngine.
- Restore the simple single-atomic tempo path in the audio thread.
- Keep `InternalSequencer::setTempoEvents` and the message-thread-side tempo-map data — just disconnect it from audio playback. The data structure stays so we can re-wire cleanly in E3.
- Tempo events currently have no playback effect after this commit (regression vs. testing observation, but no breakage either — playback is back to constant tempo from event[0]).
- Tests: existing 273 still pass.

### Phase E2 — Extract BeatState in GraphWrapper

- New `BeatState` class (probably in `src/engine/BeatState.h`).
- Owns: baseBeat, samplesSinceStart, tempo (atomics), tempoMap (atomic shared_ptr — populated from outside, walked in advance).
- One method `advance(numSamples) -> { prevBeat, nextBeat, segments }` returns the per-buffer position window and (in E3) a list of intra-buffer tempo segments.
- All mutation of beat-state atomics goes through this class. GraphWrapper holds a `BeatState beatState;` member; the scattered .store() calls in processBlock become `beatState.advance(numSamples)`.
- Tests: dedicated unit tests on `BeatState` covering constant tempo, position get/set, loop wrap, the existing behavior cases. ~10 tests.

### Phase E3 — Sample-accurate tempo events on the audio thread

- `BeatState.advance(numSamples)` consults the tempoMap. If events fall in [prevBeat, nextBeat], it builds a `vector<Segment>` where each Segment has (startSample, endSample, bpm) within this buffer.
- For the typical "no tempo change in this buffer" case, returns one segment covering the whole buffer (same as today). Zero overhead.
- For the cross case, returns 2+ segments. The MIDI scan uses segment-correct beat→sample mapping (`sampleOffset = segment.startSample + (eventBeat - segment.startBeat) / segment.beatsPerSample`).
- Audio-region start computation in `AudioFileNode` uses the same segment-aware mapping.
- After processing the buffer, BeatState atomically updates tempo (last segment's bpm), baseBeat (nextBeat), samplesSinceStart (0). One atomic transition, no scattered stores.
- Tests: BeatState advance with mid-buffer tempo crossing produces correct segments. Verified against expected sample positions.

### Phase E4 — Collapse typed event vectors into unified actionEvents

- `SongState`: remove `tempoEvents` / `timeSigEvents` / `keyEvents`. Keep `actionEvents` (rename → `events` if it doesn't churn too much).
- Register built-in actions: `setTempo` (args: `{bpm}`), `setTimeSignature` (args: `{num, den}`), `setKey` (args: `{key}`). The first two flagged audio-thread-inline.
- Lua API (`createEvent` etc.) — already collapsed in 3d-b. Internal dispatch changes: all four types become `addActionEvent(beat, actionId, args)`.
- StateAPI helpers `setSongTempo` / `getSongTempo` etc. (used by Producer LCD dial) keep working but now upsert action events with actionId=setTempo. Same observable behavior.
- Persistence: drop `tempo_events` / `timesig_events` / `key_events` tables. Everything lives in `action_events`. Migration: none — `bin/reset` for testers.
- Audio-thread tempo-map snapshot: built by filtering actionEvents for actionId==setTempo at message-thread time, atomic_stored to BeatState.

### Phase E5 — System prompt + docs + integration test

- Update `docs/AI_COMPOSITION_API.md` to reflect the unified-events end state.
- Update `runtime/SYSTEM_PROMPT.md` if any LLM-facing surface changed (probably nothing — `createEvent("tempo", ...)` still works the same).
- End-to-end manual test: tempo change at bar N produces audible change at the right beat. Audio region pre-aligned to the new tempo plays without smear. Layered material stays in sync across the change.

## Notes

- Schema changes happen across E1-E4. After this sub-project, schema_version stays at whatever it ends up — no migration code, the `bin/reset` workflow is the upgrade path.
- The AI composition merge waits until this sub-project ships. We expect ~2-3 days of careful work; no hurry, no schedule pressure.
- If at any phase we discover the architecture is wrong (e.g., sub-buffer math turns out fragile, or BeatState extraction reveals a deeper problem), we re-scope in this doc and discuss before continuing.
