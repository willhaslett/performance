# Produce-Pane Refactor — Audibility Model + Visual Layer Cleanup

This is a two-phase refactor that drops a layer of redundant state from the
core model and replaces ad-hoc paint logic in `ProducePane` with a small
explicit visual model. Triggered by repeated paint regressions when trying
to add subtle UX touches (region color tinting, mute styling) — the root
cause was that paint code reads raw state directly and composes flags in
ad-hoc order, so each new visual axis fights the existing ones.

The fix is both *less* state (drop unused/redundant audibility flags) and
*more* explicit visual intermediate types (derive what the screen needs,
then paint from that).

> Read this doc before touching any track-row, region, or plugin-slot
> rendering. Both phases interact: the visual model in Phase 2 is sized
> against the simpler state model from Phase 1, and would need re-shaping
> if Phase 1 weren't done first.

## Why this work

The current `ProducePane` paint code has three problems we hit repeatedly:

1. **Three nominally-separate "is this active" flags on a track**: `audioEnabled`, `midiEnabled`, `muted`. They overlap heavily and the boundaries are mostly mechanical (engine-graph wiring vs MIDI gating vs output silencing) rather than user-meaningful. Users see one button (mute) in DAWs they know.
2. **Painters read raw flags directly and compose them in source order.** Each new visual axis (selection, mute, drag, hover, ...) becomes another `if`, often layered with a different alpha or colour, with no central place that owns the composition rules. Adjusting one usually breaks another.
3. **Region rendering treats track and region states as overlapping flags** instead of derived concepts like "will this play?" The user mostly wants one answer per region (going to play / not), and the model should express that.

This refactor addresses all three: drop redundant flags, push the per-plugin gate down to where it conceptually belongs, and introduce derived visual structs that paint code consumes.

## Design — the model after

### Track state

Drop:

- `TrackState.audioEnabled`
- `TrackState.midiEnabled`
- `BusState.audioEnabled`
- `SongState.masterAudioEnabled`

Add:

- `EffectState.bypassed: bool` — pass-through for effect slots.
- `TrackState.instrumentBypassed: bool` — gates MIDI in / silences audio out for the instrument plugin (the only "MIDI gate" the system needs; replaces `midiEnabled` semantics).

The replacement reasoning: in Logic Pro and most modern DAWs there is no track-level on/off. There is mute (silences output) and per-plugin bypass (gates each processor). Those two cover every legitimate use case the three flags were trying to express, with the bonus that bypass is at the layer where the audio actually flows. `setActiveTrack` (the foot-pedal "switch keyboard target during performance" action) keeps its UX but is now implemented by toggling instrument-plugin bypass on the relevant tracks rather than flipping `midiEnabled`.

### API + actions

- Drop: `setTrackAudioEnabled` / `setTrackMidiEnabled` / `setBusAudioEnabled` / `setMasterAudioEnabled` (plus their getters and Lua bindings).
- Add: `setEffectBypassed` / `setInstrumentBypassed` (plus Lua bindings).
- `setActiveTrack` keeps its public name + behaviour. Internal mechanism: bypass instrument plugin on every Instrument-type track *except* the named one.
- `enableTrack` / `disableTrack` actions: drop. Mute already covers silencing.

### Engine

- `AudioEngine` always wires every track and bus into the graph. No more `enabled`-based gating.
- Bypass logic lives at the plugin slot:
  - Bypassed effect = audio passes through unchanged.
  - Bypassed instrument = MIDI not delivered to the plugin; plugin produces no audio.
- This costs a tiny bit of CPU (process-and-discard rather than not-process-at-all) but has no audible consequence at our scale.

### Persistence

- Schema column drops: `tracks.audio_enabled`, `tracks.midi_enabled`, `buses.audio_enabled`, `songs.master_audio_enabled`.
- Schema column adds: `tracks.instrument_bypassed`, `effects.bypassed`.
- No migration shim. Project is pre-beta; schema is volatile per CLAUDE.md.

### UI

- The U power icon goes away from track headers (ProducePane + TrackStrip), bus headers (BusStrip), and the master output strip (OutputStrip).
- `Theme::drawPowerButton` (just landed in commit `721ea57`) gets deleted; not precious — it was made for this icon.
- The M (mute) pill stays and absorbs the role.
- `PluginSlot` widget gains an explicit on/off (bypass) affordance for instruments. Effects already have UI for this (verify; add if missing).
- Default-song setup (`createDefaultSong`) stops referencing the removed flags.

### Visual model — the structs

```cpp
enum class Audibility { Active, Muted };

struct TrackRowVisuals {
    Audibility    audibility;       // = track.muted ? Muted : Active
    bool          selected;
    TrackSourceType type;           // for the left type stripe
};

struct RegionVisuals {
    Audibility    audibility;       // = (track.muted || region.muted) ? Muted : Active
    bool          selected;
    bool          beingDragged;
    bool          beingTrimmed;
};
```

Two pure derivation functions (`trackRowVisuals(track)`, `regionVisuals(track, region, dragState, ...)`) compute these once per paint from raw state. Two paint functions consume them:

- `paintTrackRow(g, bounds, visuals)` — owns row bg + selection compose + type stripe. Called by `paintTrackHeaders` (header column slice of the row) and `paintGrid` (lane slice of the row).
- `paintRegion(g, bounds, visuals)` — owns region bg (semi-transparent over row), content opacity, border, selection compose, drag/trim alpha overlay.

The composition order is documented in one place per scope, and adding a new visual axis (e.g., "track is recording") becomes one new field on the struct + one branch in the paint function — not five new conditionals scattered across the file.

### Visual layer model

```
1. Lane (opaque)          row colour          ← carries track.muted signal
2. Gridlines (opaque)     on top of lane      ← bar/beat lines
3. Region (semi-transp)   small lift, alpha   ← reveals lane + gridlines through
4. Region content         notes / waveform    ← carries region audibility
5. Border / selection     overlay             ← carries selection
```

Region semi-transparency means gridlines bleed through inside the region; the region reads as part of the timeline rather than a panel pasted on top. Audibility hierarchy is communicated by *both* row colour (track-mute) and region opacity (region-mute or track-mute, since they collapse to the same "won't play" signal at the region level).

Selection composes by adding rather than replacing — accent border on regions (already does this, keep), accent strip or low-alpha overlay on the row (replacing the current bg-replacement behaviour).

### Theme tokens

The exact values are placeholders to iterate after the architecture lands. Initial tokens:

- `bgRowActive` — track row background when track is unmuted (~`#1f`).
- `bgRowMuted` — same when muted (~`#1a`, recedes toward `bgPanel`).

Region uses its row colour with a small `brighter()` lift + an alpha < 1 (gridlines visible through). No separate region tokens initially; we'll add only if the region-vs-row spacing needs to differ between active and muted states (it probably doesn't).

## Build sequence

Each step is a commit; the app builds and tests pass at every step. Roll back any step that breaks something instead of patching forward.

1. **State-model field changes.** Drop `audioEnabled` / `midiEnabled` / `masterAudioEnabled`. Add `bypassed` to `EffectState` and `instrumentBypassed` to `TrackState`. Tests update.
2. **`AudioEngine` + `EngineSync` wiring change.** Always wire tracks/buses; honour the bypass fields where the gating used to live.
3. **Persistence schema.** Drop columns, add new ones. No migration shim.
4. **`StateAPI` methods + Lua bindings.** Remove the dropped setters/getters/binds; add the bypass equivalents.
5. **Action handlers.** `setActiveTrack` retargets to use bypass; `enableTrack`/`disableTrack` removed.
6. **UI removal + bypass UI.** U icon goes from all four headers. `drawPowerButton` deleted. `PluginSlot` gains/exposes bypass for instruments.
7. **Visual model.** Define the structs + derivation + paint functions. Replace scattered code in `paintTrackHeaders` + `paintGrid` + region loop.

After step 7 the tweak phase begins: starting alpha and shade values get iterated against the eye, but the architecture is settled.

## Out of scope

- The Mixer pane's `TrackStrip` / `BusStrip` / `OutputStrip` get the U icon removed but otherwise keep their existing visual treatment. The visual-model refactor (Phase 2) is `ProducePane`-specific; if the same approach helps the Mixer later, it's a follow-up.
- Region content rendering (notes velocity tinting, waveform amplitude tinting) was already cleaned up in commits `51409f4` and earlier — keep as is, just consume its `Audibility` from the new visual struct rather than re-deriving it.
- No theme picker UI work, no new tokens beyond the two added for row backgrounds.

## Open questions deferred

- Selection multi-track behaviour with the bypass-based "active track" mechanism: if multiple Instrument tracks are selected, does the keyboard route to all of them? Or just the most recent? Defer until we hit a use case in self-test.
- "Silenced by solo" visual treatment: currently invisible; the simplified `Audibility` enum could grow a `SilencedBySolo` state if it matters in practice. Out of scope for this refactor; revisit after.
