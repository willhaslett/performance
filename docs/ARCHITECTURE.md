# Architecture reference

Detailed structural diagrams and data model. For the rules, constraints,
and one-line subsystem index, see CLAUDE.md.

## Data flow

```
All mutations (GUI, Claude/Lua, IPC, MIDI bindings)
    ↓
StateAPI (in-memory state store — the runtime SSOT)
    ↓ emits StateEvent
EngineSync (subscribes to state events, applies to engine)
    ↓
AudioEngine (audio graph — a pure view of state)

PersistenceLayer (SQLite)
    loadInto: SQL → AppState struct → state.replaceState() → one event
    saveFrom: state.appState() → SQL
```

StateAPI has no JUCE dependency and no SQLite dependency. It's a pure
in-memory C++ state store, observable via `StateEventBus`. Persistence
and the audio engine are both downstream of it.

## State model (`src/state/StateModel.h`)

```
AppState
├── currentSongId
├── songs: vector<SongState>
│   ├── tracks: vector<TrackState>
│   │   ├── effects, sends
│   │   └── regions: vector<RegionState>
│   │       └── takes: vector<TakeState>  (MIDI events or audio file ref)
│   │           └── events: vector<MidiEventState>  (raw MIDI — SOT for notes)
│   ├── busses: vector<BusState>  (with effects)
│   ├── masterEffects
│   ├── bindings (song-scoped)
│   ├── score: vector<ScoreStep>
│   ├── tempoEvents, timeSigEvents  (data model only; runtime evaluation deferred)
│   └── selectedTrackIds, selectedBusIds (runtime)
├── globalBindings
├── plugins, presets, actions  (catalogs)
└── config: map<string, string>
```

### Key model facts

- **Track source types:** `Instrument` (MIDI→plugin→audio), `AudioInput` (physical input→fx→output, mono→stereo upmix), `Action` (beat-triggered, no audio, hidden from mixer).
- `midiEnabled` and `audioEnabled` are distinct; both required for MIDI routing. The power icon controls `audioEnabled` and re-enables `midiEnabled` on power-on.
- `armed`, `muted`, `soloed`, `recordModeActive` — runtime, not persisted. Recording is explicit: armed tracks record only when record mode is active.
- Regions are take folders. `MidiEventState` is raw events; notes derived via `buildNoteList()`.
- `RegionState.quantize` — non-destructive, applied at playback/display.
- Action track: one per song (auto-created), no regions — events stored directly with absolute beat positions.
- Multi-region and multi-track selection with Cmd/Shift modifiers.

## Audio graph

```
Per instrument track:
  midiInput ──────┐
  midiSourceNode ─┤→ instrument → [fx…] → outputGain ┬─ → masterGain (or bus / none)
                                                      └─ → sendGain1 → Bus1  (post-fader tap)

Per audio input track:
  audioInput[ch] ─┐
  audioFileNode ──┤→ [fx…] → outputGain ─ → masterGain / bus, + sendGain… → Bus
  (mono inputs duplicated to stereo; input optional for playback-only tracks)

Per bus:    (summed sends) → [busFx…] → busOutputGain → masterGain
Master out: masterGain → [masterFx…] → audioOutput
```

Sends are per-send **post-fader by default**: tapped from `outputGain`, so they
follow the track's mute / solo / fader. Each send has an optional **pre-fader**
mode (tapped *before* `outputGain` — the fx-chain end, or the raw file/live
sources for a no-fx audio track, since there's no single pre-fader sum node
there) and a **per-send mute** on the `sendGain` stage. Fader mode is a topology
change (rewire); send mute is just the sendGain's mute flag. See `SendState`
(`preFader`/`muted`), `AudioEngine::setSendPreFader`/`setSendMuted`, and the tap
branch in `rebuildConnections`.

`GraphWrapper` wraps the graph. In `processBlock` it: advances a sample-accurate beat clock; scans `Arrangement` for MIDI events in this buffer's beat range; routes to per-track `MidiSourceNode`s with sample offsets; captures live MIDI to `RecordFIFO` when recording; flushes notes (CC 123/120) on stop/seek/loop via per-channel bitset; then delegates to `graph.processBlock()`.

### MIDI gating

Disabled tracks (`audioEnabled=false` OR `midiEnabled=false`) receive no MIDI — both flags required in `rebuildConnections`. Actions like `setActiveTrack` set both together so the UI reflects action-driven changes.

### Audio device switching

`AudioEngine` listens to `AudioDeviceManager`. On change, `rebuildGraph()` tears down IO nodes, reconfigures, recreates and rewires. Output and input devices are independent (CoreAudio); selection persists in `config["audio_output_device"]` / `["audio_input_device"]`.

## Persistence

`src/persistence/PersistenceLayer.h/.cpp`. SQLite normalized schema at `~/.config/performance/state.db` (with `state.bak.db` backup on each save). `loadInto()` builds a plain `AppState` then calls `state.replaceState()` (atomic swap, one event). Schema version tracked. No migration shims at this stage — schema is volatile.

## Bindings & actions

Bindings map MIDI controls to named actions with arguments. Two scopes (song / global) merged via `effectiveBindings()` (song wins). Args stored as JSON arrays with track UUIDs. `paramSchema` on actions drives MappingPane input fields. `ActionInfo.durationParamIndex` identifies the duration arg for UI duration bars. `resolveTrack()` expects UUIDs only.

Built-in actions: `setActiveTrack`, `enableTrack`, `disableTrack`, `fadeOut`, `fadeIn`, `crossfade`, `trackVolume`, `morphToPreset`, `morphChain`, `morph`. SongRuntime dispatches with wildcard fallback (exact → any device → any channel → any/any).

## Plugin state presets

Stored per plugin in `~/.config/performance/snapshots/<pluginName>/<presetName>.state`. Binary blobs via JUCE `getStateInformation`/`setStateInformation`. Referenced by UUID. `PresetKind`: Instrument, Effect, Track. Track presets (`~/.config/performance/track_presets/<name>.json`) capture the full chain.

## Third-party AU plugin loading

Index `.component` Info.plist metadata at startup, on-demand `AudioComponentRegister`. Cache: `~/.config/performance/plugin-cache.xml`.
