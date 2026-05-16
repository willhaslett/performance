# Composition API redesign — ABC + content CRUD

**Parent doc:** `docs/AI_COMPOSITION.md` (research + open questions).
**Status (2026-05-13):** committed on dev branch `composition-abc-clean`. Replaces the earlier `composition-abc` which grew an audio-thread tempo refactor that didn't land. This clean branch keeps only the composition surface (ABC parser/writer, region/track/project CRUD, unified Event API, multi-event tempo/timesig/key data model + persistence). Tempo events are data-faithful and visible to the LLM, but mid-piece tempo changes don't audibly take effect during playback yet — deferred to a separate engine sub-project after merge.

## Why this exists

Two convictions from the research phase:

1. **V2 notation is not expressive enough.** It lacks dynamics, articulations, ornaments, slurs, multi-voice within a part, lyrics, mid-piece modulation, and a real ecosystem. Filling those into V2 either re-invents ABC or hits the bloat trap.
2. **ABC is a clear winner candidate.** Mature spec, in Claude's training data, mature ecosystem (renderers, MusicXML/MIDI converters), best compression of any text format per ChatMusician's analysis (~288 tokens/song avg). What ChatMusician, ComposerX, and NotaGen all chose for the same reason.

We may eventually replace this whole roll-your-own approach with a specialist symbolic-music model (ChatMusician, MIDI-LLM, or a hybrid). But ABC + content CRUD is the right next step regardless — a specialist model would still benefit from a clean content API to operate against, and ABC is the most likely interchange format with such a model anyway.

## What this changes

Three things, in order of significance:

1. **Notation: V2 → ABC.** Wherever we currently emit / parse / show V2, we use ABC instead.
2. **Composition surface: single `compose()` verb → CRUD across project / track / region scopes.** The LLM gains the ability to read existing musical content, edit in place, and write at any granularity — not just generate fresh regions.
3. **Tempo / time signature / key become first-class CRUD resources** with full event-list semantics (multi-event, addressed by beat). Previously UI-only and constant-per-project.

## The API surface

Legend: ✓ exists today · ★ new · ⤳ rename suggested · ⌫ removal proposed

| Category | Function | | Purpose |
|---|---|---|---|
| **Projects** *(backend = song; UiTerms alias to "Project")* | `song(name)` | ✓ | create or set the active project |
| | `listSongs()` | ✓ | list projects |
| | `loadSong(name)` | ✓ | load by name |
| | `unloadSong()` | ✓ | unload current |
| | `currentSongId()` | ✓ | UUID of active project |
| | `save()` | ✓ | force flush to SQLite |
| | `saveInitialState()` / `loadInitialState()` | ✓ | snapshot / restore checkpoint |
| **Tracks (structural)** | `createTrack(name)` | ✓ | empty instrument track |
| | `createAudioInputTrack(name, start, count)` | ✓ | empty audio-input track |
| | `removeTrack(name)` | ✓ | delete |
| | `selectTrack(name)` | ✓ | focus + select |
| | `setTrackInputChannels(track, start, count)` | ✓ | wire input channels |
| | `setTrackInputMonitoring(track, enabled)` | ✓ | I pill |
| **Plugins / Instruments / Effects** | `addInstrument(track, plugin [, preset])` | ✓ | load AU instrument |
| | `addEffect(parent, name, plugin)` | ✓ | parent = track / bus / "Output" |
| | `addTrackEffect` / `addBusEffect` | ✓ | aliases |
| | `removeEffect(parent, effectId)` | ✓ | |
| | `openEditor(track [, effect])` | ✓ | |
| | `listPlugins()` | ✓ | |
| | `setParam(track, name, value)` / `getParam(...)` | ✓ | |
| | `setEffectParam(parent, fx, name, value)` / `getEffectParam(...)` | ✓ | |
| | `savePreset(track, name)` / `loadPreset(track, name)` | ✓ | |
| | `morphToPreset(track, name, dur, easing)` | ✓ | |
| | `listPresets(plugin)` | ✓ | |
| **Routing / Gain / Sends / Busses** | `setTrackGainDb` / `setTrackGain` / `getTrackGain` | ✓ | |
| | `setMasterGain` / `setChannelGain` | ✓ | |
| | `createBus(name)` / `removeBus(name)` | ✓ | |
| | `setBusGainDb` / `setBusGain` | ✓ | |
| | `addSendDb` / `addSend` / `setSendGainDb` / `setSendGain` | ✓ | |
| **Devices / Bindings / Action definitions** | `registerDevice(name, port)` | ✓ | |
| | `addDeviceControl` / `addDeviceToSong` / `listDevices` | ✓ | |
| | `getDeviceControl` / `listDeviceControls` / `listMidiInputs` | ✓ | |
| | `bind(type, ch, num, action, args, desc, devId)` | ✓ | |
| | `defineAction(name, label, lua, schema, songId)` | ⤳ | renamed from `createAction` |
| | `removeAction(actionId)` | ✓ | unregister an action definition |
| | `triggerAction(actionName)` | ✓ | fire one immediately |
| **Transport / Mode / Focus / Looper** | `togglePlay()` | ✓ | |
| | `setMode("looper" \| "arrangement")` / `getMode()` | ✓ | |
| | `focusPrevTrack()` / `focusNextTrack()` / `toggleFocusedMute()` | ✓ | |
| | `setCycleLength(beats)` / `getCycleLength()` | ✓ | |
| | `setPendingTake(regionId, takeId)` | ✓ | |
| | `replaceLoop()` / `overdubLoop()` / `undoLoop()` / `redoLoop()` | ✓ | |
| | `clearLoop()` / `clearAllLoops()` / `resetLooperSession()` | ✓ | |
| | `getLoopActionState()` | ✓ | |
| **Project content (NEW)** | `getProject()` | ★ | ABC of current project (read-only) |
| **Track content (NEW)** | `listTracks()` | ⤳ | exists as `registryList("track")`; alias for symmetry |
| | `getTrack(name)` | ★ | ABC of all musical content on the track (read-only) |
| **Region content (NEW)** | `listRegions(track)` | ★ | `[{beat, length, name}, ...]` |
| | `getRegion(track, beat)` | ★ | ABC of region at that beat |
| | `setRegion(track, beat, abc)` | ★ | update only — region must exist |
| | `createRegion(track, beat, abc)` | ★ | explicit creation |
| | `deleteRegion(track, beat)` | ★ | |
| | `moveRegion(track, oldBeat, newBeat)` | ★ | structural move (form-restructuring) |
| **Action events (NEW)** | `listActionEvents(track)` | ★ | `[{id, beat, action, args}, ...]` |
| | `getActionEvent(id)` | ★ | one event |
| | `createActionEvent(track, beat, action, args)` | ★ | returns new id |
| | `setActionEvent(id, beat, action, args)` | ★ | update |
| | `deleteActionEvent(id)` | ★ | |
| **Tempo (NEW)** | `listTempos()` | ★ | `[{beat, bpm}, ...]` sorted |
| | `getTempo(beat)` | ★ | effective BPM at that beat |
| | `createTempo(beat, bpm)` | ★ | new event |
| | `setTempo(beat, bpm)` | ★ | update event at exact beat (must exist) |
| | `deleteTempo(beat)` | ★ | |
| **Time Signature (NEW)** | `listTimeSignatures()` | ★ | `[{beat, num, den}, ...]` |
| | `getTimeSignature(beat)` | ★ | effective signature at that beat |
| | `createTimeSignature(beat, num, den)` | ★ | new event |
| | `setTimeSignature(beat, num, den)` | ★ | update |
| | `deleteTimeSignature(beat)` | ★ | |
| **Key Signature (NEW — optional)** | `listKeys()` | ★ | `[{beat, key}, ...]` — empty if none set |
| | `getKey(beat)` | ★ | effective key, or `nil` |
| | `createKey(beat, key)` | ★ | string: `"C"`, `"Am"`, `"F#mix"` |
| | `setKey(beat, key)` | ★ | update |
| | `deleteKey(beat)` | ★ | |
| **Composition (current — to be removed)** | `compose(notation [, startBeat])` | ⌫ | superseded by CRUD content layer |
| **Other / utility** | `listAudioDevices()` / `setAudioDevice` / `setAudioInputDevice` | ✓ | |
| | `listInputChannels()` / `registryList(type, ...)` | ✓ | |
| | `log(msg)` / `dB(value)` / `bounce(...)` | ✓ | |
| | `interpolate(...)` / `delay(...)` / `cancel(...)` / `cancelAll()` | ✓ | |

### Renames + removal summary

- `createAction` → `defineAction` (frees the verb for the new event-scheduling op).
- `registryList("track")` → `listTracks()` (alias, both keep working).
- `compose()` removed — fully replaced by content CRUD.
- **`setProject` and `setTrack` removed (2026-05-09).** Originally planned as bulk-mutation verbs with diff-detection. Replaced by the LLM coordinating sequences of region/transport verbs (parallel tool use makes the cost of multiple verbs trivial). `getProject` / `getTrack` survive as read-only views. See "Why no `setProject`" below.

### Rules the new API needs the prompt to enforce

- **Mutate at region scope or below.** All structural changes go through verb sequences (`createRegion`, `setRegion`, `moveRegion`, `deleteRegion`, plus Phase 3's transport verbs). Use parallel tool calls in one assistant turn to compose multi-region edits.
- **Before composing into existing material, read the relevant scope first.** `getRegion` for one, `getTrack` to scan a track, `getProject` for the whole picture.
- **`P:B<n>` beat-based labels** are mechanical identifiers in `getTrack` / `getProject` ABC text. Round-trip through them, but they don't carry meaning — the same `beat` you'd pass to `getRegion(track, beat)`.

### Why no `setProject`

Briefly captured because the original plan committed to it: `setProject(abc)` was meant as a one-shot "replace whole musical content" verb with diff-detection to preserve un-touched regions. In the design pass it became clear:

- Diff inference + authority semantics (preserve vs delete absent regions) + hand-recorded protection + tempo/time-sig handling + track-creation rejection = a state-management surface with five+ branching rules in one entry point. Each rule a place subtle bugs let data corruption through.
- Round-trip fragility lives at exactly the seam where setProject expects faithful echo of un-touched material.
- Token cost is *higher* than per-verb mutation: setProject must transmit the full project ABC even when changing one region; per-verb sends only the verbs called.
- LLM tool use is *good* at "many small verbs in one parallel batch." We were going to build complexity to accommodate a pattern the LLM doesn't actually need.
- Atomicity (the one thing setProject would have offered that batched calls don't) isn't worth the cost. If we ever genuinely need transactional sequences, that's a separate `begin`/`commit` primitive, not a baked-in side-effect of one verb.

## Hard problems we're committing to solve

1. **MIDI → ABC transcription.** When the user records or hand-edits MIDI, we have raw events, not ABC. Going back to musically-correct ABC is a known-hard problem (rhythm quantization, voicing inference, articulation guess). **V1 punt:** only emit ABC for material the LLM composed itself (where we have the source ABC stored alongside the MIDI). For hand-recorded / hand-edited regions, return a placeholder (`% region recorded by user; notation unavailable`) so the LLM knows it exists but doesn't try to read it as notation. Best-effort transcription comes later.
2. **Round-trip stability via "house style".** When the LLM reads region X as ABC, edits, writes back, then reads it again later, version C should look very close to version B. Means our generator and the LLM's emitter need to converge on the same ABC conventions: bar-line frequency, voice naming, key inference, default note length, header field order. Document as a "house style" in the system prompt and produce matching output from the project-state-to-ABC writer.
3. **ABC parser robustness for what the LLM emits.** The spec is mature but full of edge cases. We rolled our own parser (~470 lines) covering the subset the LLM produces, rejecting unsupported constructs explicitly so material isn't silently lost.

## What's deliberately out of scope this pass

- **Multi-agent decomposition (ComposerX-style).** Stays in the brainstorm pile until we see how the new API performs single-agent.
- **Specialist-model integration (AMT / ChatMusician / MIDI-LLM as tools).** Same — defer until baseline is in.
- **Sketch-input / hum-to-MIDI / infill UX.** Out of scope; this redesign is about the read/write surface, not the user-input mode.
- **RLHF-style automated quality judges.** Out of scope.
- **Best-effort transcription of hand-recorded MIDI.** Punted explicitly above.

## Implementation plan

Sequential phases, each landable as a separate commit (or small commit set) on `composition-abc`. Each phase leaves the tree buildable; tests pass at every step.

### Phase 1 — ABC infrastructure

- ABC parser (`src/composer/ABCParser.h/.cpp`): consumes ABC, emits `ComposerOutput` with `MidiEventState`s. Mirrors the role of `V2NotationParser`. Supports the subset the LLM actually emits (notes, chords, rests, ties, durations, basic dynamics, multi-voice via `V:`, repeats, headers including `K:none`).
- ABC writer (`src/composer/ABCWriter.h/.cpp`): given `MidiEventState`s + region/track context, emits ABC. Used for `getRegion / getTrack / getProject`. Establishes "house style" conventions.
- Tests: ABC → MIDI → ABC round-trip stability for representative cases (single voice, multi-voice, ties, repeats, varying time-sig).

### Phase 2 — Content CRUD Lua API

**Phase 2a (shipped):** `listTracks` / `listRegions` / `getRegion` / `setRegion` / `createRegion` / `deleteRegion` via `RegionContent` bridge. Tests cover each op + round-trip preservation.

**Phase 2b (next):**
- `getTrack(name)` — read-only stitched view of a track's regions, single voice with `P:B<n>` beat-labels per region, rests filling gaps between regions.
- `getProject()` — read-only project-wide view, multi-voice (one `V:` per track), `P:B<n>` labels per region within each voice.
- `moveRegion(track, oldBeat, newBeat)` — structural move primitive. Errors if newBeat collides with another region.
- Tests: round-trip via parser confirms read views are parseable; moveRegion respects collisions and emits Track::Updated.

`setProject` / `setTrack` deliberately omitted — see "Why no `setProject`" above.

`ProjectDiff.h/.cpp` no longer needed.

### Phase 3 — Tempo / time signature / key CRUD

- `KeyEvent` added to `StateModel.h` parallel to `TempoEvent` / `TimeSignatureEvent`.
- SQLite schema additions (key events table). No migration needed — schema versioning bump only.
- Lua exposure for all three resource families.
- ABC writer integrates header (Q:, M:, K:) and inline (`[Q:1/4=120]`, `[M:3/4]`, `[K:G]`) emission.
- Tests: events at various beats round-trip through ABC; effective-value lookups (`getTempo(20)` returns the most-recent-prior event).

### Phase 4 — Action event CRUD + rename

- Rename `createAction` → `defineAction` in `LuaEngine.cpp` (one call site rename in `runtime/SYSTEM_PROMPT.md`).
- New `listActionEvents` / `getActionEvent` / `createActionEvent` / `setActionEvent` / `deleteActionEvent`.
- Tests: standard CRUD on action events.

### Phase 5 — System prompt rewrite

- Replace V2 grammar section in `runtime/SYSTEM_PROMPT.md` with ABC primer + house-style conventions.
- Add CRUD usage rules: smallest-scope-that-contains-effect, read-before-write, use named regions / `P:` labels.
- Prompt cache will reset; first request after deploy pays full cost again.

### Phase 6 — Remove old surface

- Delete `compose()` Lua function.
- Delete `V2NotationParser`, `ComposerWriter` (the V2 one — keep the new ABC writer obviously).
- Update `docs/COMPOSER_INTEGRATION.md` to mark V2 path as historical / superseded.
- Update `docs/AI_COMPOSITION.md` to mark this redesign as shipped.

### Phase 7 — End-to-end validation before merge

- Manual test: empty project → compose piano via chat → compose drum that responds to it → edit a region → restructure with a bridge insertion → playback works correctly throughout.
- Verify diff-detection: ask the LLM to do a setProject that "doesn't intend to change" a hand-recorded region; verify the recording survives.
- Token cost check: run a representative session and confirm caching keeps per-request input cost in line with current numbers.
- Listen test: subjective, comparing AI-composed output now vs. before. Doesn't have to dramatically improve — just shouldn't regress, and should unlock new editing/extending operations that weren't possible before.

### Merge

When phase 7 looks good, merge `composition-abc` to `main`. Bump version. Update CLAUDE.md + AI_COMPOSITION.md "current focus" sections. Ship to friend testers in next round (no rush — tester feedback so far is light).

## Migration / data preservation

- **Existing songs in user DBs:** stored as `MidiEventState`s — format-agnostic. No data migration needed; the new API just generates ABC from the same underlying events.
- **AI-composed regions in existing songs:** their stored events were V2-derived. We don't have the V2 source preserved alongside, so we'll have to transcribe them to ABC using the writer. For most regions this should round-trip cleanly since the events came from a parser. For genuinely complex cases (very dense polyphonic material), the transcription may produce ABC that differs in formatting from what would be ideal — acceptable cost.
- **Conversation history with `compose()` calls:** still valid history, but the function it referenced no longer exists. Acceptable — old conversations don't get retroactively fixed; new conversations use the new surface.

## Notes

- The `composition-abc` branch is for this work specifically. Side fixes / unrelated bugs that come up during testing should land on `main` directly so the branch stays focused.
- Token cost on the LLM side: prompt caching keeps the per-request overhead low. The big cost driver will be initial fetches of project / track ABC into context. Will be fine.
- We may discover during phase 1 or 2 that round-trip stability or transcription is harder than we think. If so, we re-scope, document the new findings in this doc, and decide whether to proceed.
