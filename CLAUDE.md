# Live Performance Environment

A scriptable runtime for live music performance on macOS. Solo performer, centered around an Arturia KeyLab 88 MkII and Audio Unit plugins. The app is a live environment — always running, always ready. An in-memory state store is the single source of truth at runtime. SQLite is the persistence layer (load on startup, save on demand). The audio engine is a pure view of state.

## Core Concepts

- **The app is an environment** — it launches and restores its previous state from SQLite into the in-memory state store. Save on quit and on explicit save. Tracks, instruments, effects, sends, gains, and presets all persist automatically.
- **Sandbox** — the permanent scratchpad session. Always exists, always at the top of the sidebar, undeletable. The user can experiment freely without affecting any song. On launch, the app restores the last active session (sandbox or a song).
- **Song** — a named session with its own tracks, busses, sends, bindings. Switching songs clears the engine and rebuilds from state.
- **Bindings** — MIDI controls bind to named actions (e.g., `setActiveTrack`, `fadeOut`) with entity ID arguments. Two scopes: global (always active) and song-scoped (override globals, deleted with song). All behavior is a registered, reusable action.
- **Score** — an ordered list of action references per song. Used for development ("go to step N" replays from initial state) and as documentation of performance transitions.
- **Automation** — `interpolate(from, to, duration, callback, easing)` with library helpers: `fadeOut`, `fadeIn`, `crossfade`, `paramSweep`.
- **Authoring model** — Claude runs embedded in the app (native chat UI calling Claude API with tool use). Will plays and directs, Claude modifies the environment via the `perf` tool (Lua execution). The GUI provides direct manipulation. All consumers use the same APIs.

## Architecture

### Data Flow

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

### Three APIs

- **StateAPI** (`src/api/StateAPI.h/.cpp`) — all state reads/writes. In-memory C++ structs (`src/state/StateModel.h`), observable via `StateEventBus` (`src/state/StateEvents.h`). No JUCE dependency, no SQLite. Every consumer defaults to this.
- **EngineAPI** (`src/api/EngineAPI.h/.cpp`) — engine-only concerns: peak levels, processor access (for presets/params), plugin editor windows, plugin discovery. Uses `juce::String`. Use only when you need something the state store can't provide.
- **PerformanceCoordinator** (`src/api/PerformanceCoordinator.h/.cpp`) — lifecycle and orchestration: init/shutdown, song management, track presets (cross-cutting), automation, action dispatch. Owns all subsystems, exposes `state()` and `engine()` to consumers.

### State Model (`src/state/StateModel.h`)

```
AppState
├── currentSongId
├── songs: vector<SongState>
│   ├── tracks: vector<TrackState>
│   │   ├── effects: vector<EffectState>
│   │   └── sends: vector<SendState>
│   ├── busses: vector<BusState>
│   │   └── effects: vector<EffectState>
│   ├── masterEffects: vector<EffectState>
│   ├── bindings: vector<BindingState>  (song-scoped)
│   ├── score: vector<ScoreStep>
│   └── selectedTrackIds, selectedBusIds (runtime, not persisted)
├── globalBindings: vector<BindingState>
├── plugins: vector<PluginInfo>  (catalog)
├── presets: vector<PresetInfo>  (catalog)
├── actions: vector<ActionInfo>  (catalog)
└── config: map<string, string>
```

Key model features:
- Effects/sends nested inside parent (no flat table lookup)
- `LoadStatus` on TrackState and EffectState (None/Pending/Loaded/Failed)
- `isInstrument` on PluginInfo (GUI builds plugin menus from state)
- `PresetKind` enum (Instrument/Effect/Track)
- Selection state on SongState (observable, not persisted)

### Persistence (`src/persistence/PersistenceLayer.h/.cpp`)

SQLite normalized relational schema. `loadInto()` builds a plain `AppState` struct from SQL (no StateAPI mutators, no events, no ID conflicts), then calls `state.replaceState()` which atomically swaps state and fires one event. `saveFrom()` flushes state to SQL.

Database: `~/.config/performance/state.db`

Tables: `plugins` (with `is_instrument`), `presets` (with `kind`), `actions`, `songs`, `tracks`, `busses`, `effects`, `sends`, `bindings` (nullable `song_id` for global/song scope), `score_steps`, `config`.

### Identity

UUID everywhere. Every track, bus, effect, send has a UUID assigned at creation. Names are display properties only. All APIs, engine, GUI, and internal references use UUIDs. Names resolved to UUIDs once at Lua/Claude entry point.

### Rules for new code

- All state reads/writes go through StateAPI. Never call audioEngine for state.
- EngineAPI is for peak levels, processors, plugin UI, and plugin discovery only.
- EngineSync is the only code that calls AudioEngine write methods.
- NEVER use names as keys or identity. Use UUIDs.
- When adding a new settable value: add to StateModel, add StateAPI method, handle in EngineSync.

### Other Components

- **EngineSync** (`src/engine/EngineSync.h/.cpp`) — pure event subscriber. Subscribes to StateAPI events, applies to engine. Zero public methods.
- **AudioEngine** (`src/engine/AudioEngine.h/.mm`) — JUCE AudioProcessorGraph. All methods accept UUIDs. Pure view of state.
- **MIDIEngine** (`src/engine/MIDIEngine.h/.cpp`) — MIDI input, forwards notes to audio graph, dispatches controls to SongRuntime.
- **AutomationEngine** (`src/automation/AutomationEngine.h/.cpp`) — 60fps timer, interpolations with easing.
- **LuaEngine** (`src/scripting/LuaEngine.h/.cpp`) — embedded Lua via sol2. Takes StateAPI+EngineAPI+Coordinator.
- **IPCServer** (`src/ipc/IPCServer.h/.cpp`) — Unix domain socket `/tmp/performance.sock`. `bin/perf` shell command.
- **SongRuntime** (`src/song/SongRuntime.h/.cpp`) — MIDI control dispatch map.

### GUI

All GUI components take `StateAPI&` + `EngineAPI&` (no PerformanceAPI).

- **MainLayout** — root container: toolbar + sidebar + split panes + mixer
- **MixerView** — track/bus/output strips, 30Hz poll (state for structure, engine for peak levels)
- **TrackStrip** — instrument slot, effect slots, sends panel, fader+meter. Track preset callbacks from coordinator.
- **BusStrip** — effect slots, fader+meter
- **OutputStrip** — master effect slots, master fader+meter
- **PluginSlot** — reusable pill with picker, context menu, auto-open on load. Uses StateAPI for plugin resolution, EngineAPI for editor/presets.
- **SendsPanel** — StateAPI only. Pill+knob rows with signal glow.
- **Sidebar** — StateAPI only. Songs, Library (instruments/effects with presets), Actions.
- **FaderMeter**, **InlineEditor**, **SaveAsDialog**, **Theme**, **PaneContainer**, **ChatView**, **ClaudeClient**

### Audio Graph

```
Per track:
  midiInput → instrument → [fx1 → fx2 →] ┬─ outputGain → masterGain
                                           ├─ sendGain1  → Bus1
                                           └─ sendGain2  → Bus2
Per bus:
  (summed sends) → [busFx1 → busFx2 →] busOutputGain → masterGain

Master output:
  masterGain → [masterFx1 → masterFx2 →] → audioOutput
```

### Plugin State Presets

Stored per plugin in `~/.config/performance/snapshots/<pluginName>/<presetName>.state`. Binary blobs via JUCE `getStateInformation`/`setStateInformation`. Referenced by UUID in state. Independent of songs. `PresetKind`: Instrument, Effect, Track.

Track presets: `~/.config/performance/track_presets/<name>.json` — full chain (instrument + state + effects + sends + gain + MIDI).

### IPC

`bin/perf` shell command sends Lua to the running app. `runtime/CLAUDE.md` is the prompt for the embedded Claude instance.

### Third-party AU Plugin Loading

Index .component bundle Info.plist metadata at startup, on-demand register via AudioComponentRegister. Cache: `~/.config/performance/plugin-cache.xml`.

## Test Suite

49 tests:
- StateAPI tests (34): full in-memory state store coverage
- Persistence round-trip tests (3): save→load fidelity, multi-song, empty DB
- Integration tests (12): full coordinator→state→EngineSync→engine path

## TODOs

**Next up:**
- Plugin state restore on session load — instruments load but preset state (binary blob) isn't restored. EngineSync should call setStateInformation after async load completes, using the preset referenced in TrackState/EffectState. LoadStatus field designed for this.
- Verify IPC/perf tool works end-to-end with new system (IPCServer → LuaEngine → StateAPI)
- Delete `src/registry/` directory if still present. Check if RegistryTree.h/.cpp is still used by Sidebar or dead.
- Track preset state restore uses 500ms timer hack — should use LoadStatus callback
- No error handling on failed plugin loads (user sees nothing)

**Feature backlog (near-term):**
- Customizable keyboard shortcuts — KeyBindings.h has defaults, future settings UI overrides from config. Runtime lookup instead of compile-time constants.
- MIDI Learn / device management — ad hoc learn/mapping + persisted device maps
- Global bindings from GUI — "MIDI Learn" mode (move a control → bind)
- Score authoring — model exists (ScoreStep) but no UI or Lua API to build/replay
- Auto-create Default preset on first plugin instantiation
- AUPitch: preset state restore doesn't take effect (AU-specific issue)
- Live audio tracks (input from audio device, same track model)
- Undo/redo via state history
- MIDI device hot-plug
- MIDI effects (transpose, channel filter, arpeggiator)
- Audio device configuration (buffer size, sample rate)
- Fader/knob drag: value stops changing at screen edge

**Low priority:**
- juce_String.cpp:327 assertion on startup — non-fatal, likely JUCE internals
