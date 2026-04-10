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
- Two track source types: `TrackSourceType::Instrument` (MIDI→plugin→audio) and `TrackSourceType::AudioInput` (physical input→effects→output, mono→stereo upmix)
- `midiEnabled` (MIDI note routing, instrument tracks) and `audioEnabled` (audio signal pass-through, all tracks) are distinct properties
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
- **MIDIEngine** (`src/engine/MIDIEngine.h/.cpp`) — MIDI input, forwards notes to audio graph, dispatches controls to SongRuntime. Supports device-specific monitoring and a global monitor (for debug pane). MIDI Learn with single-shot capture.
- **AutomationEngine** (`src/automation/AutomationEngine.h/.cpp`) — 60fps timer, interpolations with easing.
- **LuaEngine** (`src/scripting/LuaEngine.h/.cpp`) — embedded Lua via sol2. Takes StateAPI+EngineAPI+Coordinator.
- **IPCServer** (`src/ipc/IPCServer.h/.cpp`) — Unix domain socket `/tmp/performance.sock`. `bin/perf` shell command.
- **SongRuntime** (`src/song/SongRuntime.h/.cpp`) — MIDI control dispatch map.

### GUI

All GUI components take `StateAPI&` + `EngineAPI&` (no PerformanceAPI).

- **MainLayout** — root container: toolbar + sidebar + flexible dual-pane area + mixer. Left pane (Device Editor or Debug) and right pane (Chat or Logs) switchable via sidebar.
- **MixerView** — track/bus/output strips, 30Hz poll (state for structure, engine for stereo peak levels)
- **TrackStrip** — instrument slot (or input selector for audio input tracks), effect slots, sends panel, fader+stereo meters with IEC-scale dB labels. Power icon toggles `audioEnabled`. Track preset callbacks from coordinator.
- **BusStrip** — effect slots, fader+stereo meters
- **OutputStrip** — master effect slots, master fader+stereo meters
- **FaderMeter** — fader + dual L/R meters on IEC-style non-linear dB scale (-60 to +6). Peak hold with exponential decay. Grid lines, color zones (green/amber/red at -12/0dB), dB tick labels. Fader drag operates in normalized space through the curve.
- **PluginSlot** — reusable pill with picker, context menu, auto-open on load. Uses StateAPI for plugin resolution, EngineAPI for editor/presets.
- **SendsPanel** — StateAPI only. Pill+knob rows with signal glow.
- **Sidebar** — StateAPI + EngineAPI + PerformanceCoordinator. Songs, Library (instruments/effects with presets), Actions, Maps (MIDI devices with activity lights), Devices (Audio with per-device Input/Output children), Panes (Debug, Logs, Chat). Audio device nodes always expanded; click device name to set both I/O, click Input or Output leaf individually. Green dot on active role/activity.
- **MappingPane** — Unified device mapping + bindings + score pane. Page title "Mappings" with device/song context. Single table with columns: MIDI Source, Score Step, Group, Type, Ch, #, Action. Score steps sort to top. Score Step column is a clickable integer selector with insert/replace semantics. Learn mode with default names, inline name/group editing, action assignment via popup with param dialogs, right-click to delete. Effect plugins grouped by manufacturer in picker menus.
- **DebugPane** — Dev-time diagnostic view: live MIDI event log (all devices, color-coded by type) + audio input level meters per channel.
- **LogPane** — Live tail of `/tmp/performance.log` in a selectable/copyable TextEditor. Auto-scrolls.
- **SettingsWindow** — popup window (Cmd+,) with tabbed interface. Audio tab: output/input device, buffer size, sample rate, computed latency. MIDI tab placeholder. Also accessible via Performance menu → Settings.
- **InlineEditor**, **SaveAsDialog**, **Theme**, **PaneContainer**, **ChatView**, **ClaudeClient**

### Audio Graph

```
Per instrument track:
  midiInput → instrument → [fx1 → fx2 →] ┬─ outputGain → masterGain
                                           ├─ sendGain1  → Bus1
                                           └─ sendGain2  → Bus2
Per audio input track:
  audioInput[ch] → [fx1 → fx2 →] ┬─ outputGain → masterGain
                                   ├─ sendGain1  → Bus1
                                   └─ sendGain2  → Bus2
  (mono inputs: channel duplicated to stereo at first node)

Per bus:
  (summed sends) → [busFx1 → busFx2 →] busOutputGain → masterGain

Master output:
  masterGain → [masterFx1 → masterFx2 →] → audioOutput
```

Audio device switching: `AudioEngine` implements `ChangeListener` on `AudioDeviceManager`. On device change, `rebuildGraph()` tears down IO nodes, reconfigures graph for new device's channel count, recreates IO nodes, rewires connections. If device goes null (mid-transition), processing stops cleanly and recovers on next notification. `InputMeter` callback provides per-channel peak levels for the debug pane.

Audio output and input devices are independent (macOS CoreAudio). Selection persists via `config["audio_output_device"]` and `config["audio_input_device"]` — restored on startup after `loadInto`. Only `outputDeviceName` or `inputDeviceName` is set (never both to the same value, which fails for output-only devices like MacBook Pro Speakers). Clicking the same device is a no-op. If device goes null mid-transition, async retry recovers once JUCE finishes opening the new device.

MIDI gating: disabled tracks (`audioEnabled=false`) receive no MIDI — prevents wasted synthesis across multiple instrument tracks. `midiEnabled` and `audioEnabled` are both required for MIDI connection in `rebuildConnections`. The power icon controls `audioEnabled` and also re-enables `midiEnabled` when turning a track on. Actions (`setActiveTrack`, `enableTrack`, `disableTrack`) set both `audioEnabled` and `midiEnabled` together so the UI reflects action-driven changes.

### Bindings & Actions

Bindings map MIDI controls to named actions with arguments. Two scopes: song-scoped (deleted with song) and global (always active). `effectiveBindings()` merges both (song wins on conflict). Bindings store action args as JSON arrays with track UUIDs (resolved from names at bind-time). Action `paramSchema` (JSON) defines expected parameters — used by MappingPane to generate appropriate input fields.

Built-in actions: `setActiveTrack(trackName)`, `enableTrack(trackName)`, `disableTrack(trackName)`, `fadeOut(trackName, duration, easing)`, `fadeIn(trackName, duration, easing)`, `crossfade(fromTrack, toTrack, duration, easing)`. Track args stored as UUIDs (resolved at bind-time). `resolveTrack()` expects UUIDs only — no name fallback at runtime.

SongRuntime dispatches MIDI events to bindings with wildcard fallback: exact match → any device → any channel → any device + any channel.

**All bindings are song-scoped.** Global bindings are deferred — the data model supports them but the UI only creates song bindings. Track UUIDs in binding args belong to the song where they were created. Future: "Copy to song" or global bindings for song-agnostic actions (reset, save, next song).

### Maps (unified device mapping + bindings + score)

MappingPane (`src/gui/MappingPane.h/.cpp`) — single table per device. Accessible via "Maps" sidebar section with per-device MIDI activity lights. All bindings are song-scoped — switching songs refreshes bindings (device controls persist). Score steps sort to top of the table. Score Step is a clickable integer selector: "Not in score", "Step N (append)", or insert before/after/replace existing steps with automatic position bumping.

### Logging

`perfLog()` writes to stderr and `/tmp/performance.log` (unbuffered, ISO 8601 UTC timestamps). Subsystems prefix: `[Engine]`, `[EngineSync]`, `[Coordinator]`, `[MIDI]`, `[Persistence]`, `[Sidebar]`, `[IPC]`. Tail with `tail -f /tmp/performance.log`. In-app LogPane provides selectable/searchable view.

### Plugin State Presets

Stored per plugin in `~/.config/performance/snapshots/<pluginName>/<presetName>.state`. Binary blobs via JUCE `getStateInformation`/`setStateInformation`. Referenced by UUID in state. Independent of songs. `PresetKind`: Instrument, Effect, Track.

Track presets: `~/.config/performance/track_presets/<name>.json` — full chain (instrument + state + effects + sends + gain + MIDI).

### IPC

`bin/perf` shell command sends Lua to the running app. `runtime/CLAUDE.md` is the prompt for the embedded Claude instance.

### Third-party AU Plugin Loading

Index .component bundle Info.plist metadata at startup, on-demand register via AudioComponentRegister. Cache: `~/.config/performance/plugin-cache.xml`.

## Test Suite

76 tests:
- StateAPI tests (34): full in-memory state store coverage
- Persistence round-trip tests (3): save→load fidelity, multi-song, empty DB
- Integration tests (12): full coordinator→state→EngineSync→engine path
- EngineSync tests (23): mock engine verifying state→engine event dispatch (includes audio input tracks, audioEnabled, instrument changes, song switching)
- Audio device config tests (4): device name persistence, config round-trip, audioEnabled persistence

## TODOs

**Production readiness (completed):**
1. ~~Beach ball → spinner overlay~~ — done. Semi-transparent overlay with message during save/load/song switch.
2. ~~Directory permission prompt~~ — done. Explicit AU plugin paths, no getDefaultLocationsToSearch.
3. ~~Audio buffer size / sample rate control~~ — done. Settings window (Cmd+,) with Audio tab. Also in sidebar. Persisted.
4. ~~MIDI device hot-plug~~ — done. 4Hz polling, auto add/remove callbacks.
5. ~~Error boundary~~ — done. JUCE crash handler with emergency save.

**Production readiness (remaining):**
6. Background plugin state capture: move getStateInformation calls off the message thread. Root cause of the beach ball. Hard — JUCE plugin APIs aren't thread-safe.
7. Failed plugin load feedback: user sees nothing when a plugin fails to instantiate. Need status indicator on the slot.
8. Settings window: MIDI tab placeholder exists, needs content (MIDI channel filtering, transpose, etc).

**Known functional issues:**
- Device Learn: new mappings persist immediately on capture instead of waiting for name commit. Root cause: JUCE TextEditor focusLost fires commit before cancel can intercept. Workaround: right-click → Delete.
- AUShelfFilter crashes on instantiation — plugin bug.
- AUPitch: preset state restore doesn't take effect — AU bug.
- juce_String.cpp:327 assertion on startup — non-fatal, JUCE internals.
- No error handling on failed plugin loads (user sees nothing).
- State changes sometimes not visible until restart — watch for missed rebuildConnections/restoreBindings calls.

**Known issues with embedded Claude:**
- Bindings created via Claude/Lua may use wrong track names (case mismatch). GUI is more reliable.
- Custom action creation via Claude fails despite API working via direct IPC. Needs investigation (string escaping through chat→tool→IPC pipeline).

**Feature backlog (near-term):**
- Customizable keyboard shortcuts — KeyBindings.h defaults → config overrides → runtime lookup.
- Undo/redo via state history — state model is clean structs, snapshot-based undo feasible.
- TempoMap + TimeSignatureMap — runtime evaluation of per-song tempo/time-sig event lists. Data model is ready (`TempoEvent`/`TimeSignatureEvent` vectors on `SongState`, sorted by beat). Runtime needs: (1) TempoMap utility — "what tempo at beat X?", "seconds from beat A to B?" (piecewise integration), "given N samples at beat X, what's the new beat?" Replaces all single-tempo arithmetic in `InternalSequencer::advance()` and `GraphWrapper::processBlock()`. (2) TimeSignatureMap — "what time sig at beat X?", "what bar/beat is beat X?", "where does bar N start?" Replaces `beatsPerBar()` single-value return. (3) GraphWrapper steps through tempo events within each buffer for correct sample offsets across tempo changes. (4) ProducePane grid renders variable-width bars at time sig boundaries. Current code uses single tempo/time-sig everywhere — all callsites are identified and use sequencer queries, so swapping in map lookups is mechanical. No trapdoors.

**Feature backlog (longer-term):**
- MIDI effects (transpose, channel filter, arpeggiator)
- Fader/knob drag: value stops changing at screen edge

## DAW Integration & Sequencer Plan

### Landscape (research summary)

**External control paths per DAW:**
- **Logic**: MCU over virtual MIDI only. No API, no OSC. AppleScript dead.
- **Ableton Live**: Max for Live + OSC bridge for full control. Ableton Link for tempo sync. Remote Scripts (undocumented Python) for MIDI-triggered control.
- **Reaper**: Native OSC (configurable), ReaScript (1500+ API functions), HTTP interface. Best external story.
- **Bitwig**: DrivenByMoss + OSC. Native Controller API (JavaScript) is solid but MIDI-input-driven.

**Universal protocols:**
- MCU: works everywhere, gives transport + 8-channel mixer + banking. No clip triggering.
- OSC: no standard schema, per-DAW mapping. Low latency, float-native.
- Ableton Link: open-source C++ tempo/phase sync. Cross-app, reliable. Not a control protocol.
- MIDI 2.0 Property Exchange: early, no DAW adoption yet. Watch, don't build on.
- Rewire: dead.

### Design: three-tier DAW bridge

```
Performance App
    ↓
DAWBridge protocol (internal C++ interface)
    ↓ implementations:
┌─ MCUBridge (virtual MIDI, works with any DAW)
├─ OSCBridge (configurable address space, native in Reaper)
├─ M4LBridge (Max for Live relay for Ableton)
├─ ReaScriptBridge (ReaScript IPC for Reaper)
└─ InternalSequencer (our own, no external DAW)
```

`DAWBridge` defines operations in our domain — implementations translate to whatever the DAW speaks. Caller never knows the transport.

### DAWBridge interface (draft)

```
Transport: play, stop, record, setTempo, getTempo, getPosition, setLoop
Tracks: armTrack, muteTrack, soloTrack, setTrackGain, getTrackGain
Clips: triggerClip, stopClip, getClipState (Ableton/Bitwig only via deep integration)
Mixer: setTrackPan, setTrackSend
Markers: gotoMarker, nextMarker, prevMarker
Sync: link (Ableton Link for tempo/phase sync)
```

Clip triggering is DAW-specific (MCU can't do it). The interface should make it optional — callers check capability.

### Implementation plan

**Phase 1: Internal Sequencer (no external DAW)**
- Define `DAWBridge` as a C++ abstract class in `src/daw/DAWBridge.h`
- Implement `InternalSequencer` as the first backend — our own transport, tempo, beat clock
- Add a transport bar to the UI (play/stop/record/tempo/position)
- Drive tempo from internal clock, sync audio callback to beat position
- This gives us a metronome and beat-synced automations without any DAW

**Phase 2: Ableton Link**
- Embed the Link library (header-only, Apache 2.0)
- `LinkBridge` syncs our internal tempo/phase to Link-enabled apps
- No control of other apps — just tempo lock

**Phase 3: MCU bridge**
- `MCUBridge` sends/receives MCU protocol over CoreMIDI virtual port
- Any DAW sees us as a Mackie control surface
- Gets transport + mixer for free in Logic, Reaper, Live, Bitwig

**Phase 4: OSC bridge**
- `OSCBridge` sends/receives OSC over UDP
- Ship with Reaper .ReaperOSC mapping file
- Community can write mappings for other DAWs

**Phase 5: Deep integration (if warranted)**
- M4L device for Ableton (clip triggering, full LOM access)
- ReaScript for Reaper (direct API calls)
- Each is a separate adapter behind `DAWBridge`

### Constraints
- `DAWBridge` lives in `src/daw/` — clean boundary, no tentacles into `src/engine/` or `src/api/`
- The audio engine continues to own all plugin hosting and audio routing
- The sequencer is a consumer of `StateAPI` (writes tempo/position to state) and `AudioEngine` (for metronome/click)
- If the bridge fails or disconnects, the app continues functioning — degraded, not broken

## LOC

~15,000 lines of source code (headers + implementation + tests). See `find src tests -name "*.h" -o -name "*.cpp" -o -name "*.mm" | xargs wc -l`.
