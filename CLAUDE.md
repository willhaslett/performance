# Live Performance Environment

A scriptable runtime for live music performance on macOS. Solo performer, centered around an Arturia KeyLab 88 MkII and Audio Unit plugins. The app is a live environment — always running, always ready. An in-memory state store is the single source of truth at runtime. SQLite is the persistence layer (load on startup, save on demand). The audio engine is a pure view of state.

## Core Concepts

- **The app is an environment** — it launches and restores its previous state from SQLite into the in-memory state store. Save on quit and on explicit save. Tracks, instruments, effects, sends, gains, and presets all persist automatically.
- **Sandbox** — the permanent scratchpad session. Always exists, always at the top of the sidebar, undeletable. The user can experiment freely without affecting any song. On launch, the app restores the last active session (sandbox or a song).
- **Song** — a named session with its own tracks, busses, sends, bindings. Switching songs clears the engine and rebuilds from state.
- **Bindings** — MIDI controls bind to named actions (e.g., `setActiveTrack`, `fadeOut`) with entity ID arguments. Two scopes: global (always active) and song-scoped (override globals, deleted with song). All behavior is a registered, reusable action.
- **Score** — an ordered list of action references per song. Used for development ("go to step N" replays from initial state) and as documentation of performance transitions.
- **Automation** — `interpolate(from, to, duration, callback, easing)` and `delay(seconds, callback)` with actions: `fadeOut`, `fadeIn`, `crossfade`, `morphToPreset`, `morphChain`, `morph` (compound). The `morph` action bundles multiple sub-actions (parallel or sequential). All actions can be placed on the action track timeline or bound to MIDI controls.
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
│   │   ├── sends: vector<SendState>
│   │   └── regions: vector<RegionState>
│   │       └── takes: vector<TakeState>  (take folders — MIDI events or audio file ref)
│   │           └── events: vector<MidiEventState>  (raw MIDI stream — SOT for notes)
│   ├── busses: vector<BusState>
│   │   └── effects: vector<EffectState>
│   ├── masterEffects: vector<EffectState>
│   ├── bindings: vector<BindingState>  (song-scoped)
│   ├── score: vector<ScoreStep>
│   ├── tempoEvents: vector<TempoEvent>  (beat→bpm changes)
│   ├── timeSigEvents: vector<TimeSignatureEvent>  (beat→time sig changes)
│   └── selectedTrackIds, selectedBusIds (runtime, not persisted)
├── globalBindings: vector<BindingState>
├── plugins: vector<PluginInfo>  (catalog)
├── presets: vector<PresetInfo>  (catalog)
├── actions: vector<ActionInfo>  (catalog)
└── config: map<string, string>
```

Key model features:
- Three track source types: `TrackSourceType::Instrument` (MIDI→plugin→audio), `TrackSourceType::AudioInput` (physical input→effects→output, mono→stereo upmix), and `TrackSourceType::Action` (beat-triggered actions, no audio, hidden from mixer)
- `midiEnabled` (MIDI note routing, instrument tracks) and `audioEnabled` (audio signal pass-through, all tracks) are distinct properties
- `armed` (runtime, not persisted) — record-arm; armed tracks record MIDI when the sequencer is playing
- `audioEnabled` on BusState and `masterAudioEnabled` on SongState — power toggle for busses and master output
- Effects/sends nested inside parent (no flat table lookup)
- `LoadStatus` on TrackState and EffectState (None/Pending/Loaded/Failed)
- `isInstrument` on PluginInfo (GUI builds plugin menus from state)
- `PresetKind` enum (Instrument/Effect/Track)
- `TempoEvent`/`TimeSignatureEvent` — per-song tempo and time sig changes at specific beat positions (data model ready, runtime evaluation deferred)
- Regions are take folders: each `RegionState` contains `vector<TakeState>` with `activeTakeId`. Supports multi-take recording — each pass creates a new take, preserving previous ones. `MidiEventState` is the raw MIDI event (noteOn, noteOff, CC, aftertouch, etc.) — notes are derived via `buildNoteList()`, not stored.
- `TrackState.color` (uint32, 0 = type default) — user-definable track color. Instrument tracks default to `bgHeader`, audio input to amber. Regions and track headers both use this color.
- Recording is explicit via `recordModeActive` — press 'r' or click record button to enter record mode. Armed tracks record only when record mode is active.
- Selection state on SongState (observable, not persisted)
- Action track: one per song (auto-created), stores `ActionEventData` directly on the track (no regions). Events have absolute beat positions, action ID, and JSON args. Scanned during playback and dispatched on message thread.
- Non-destructive quantize: `RegionState.quantize` (grid size in beats, 0 = off). Applied at playback and display time — original event data untouched.
- Multi-region and multi-track selection with Cmd/Shift modifiers. Region operations (delete, duplicate, mute, quantize, split, join, trim) work on the full selection.

### Persistence (`src/persistence/PersistenceLayer.h/.cpp`)

SQLite normalized relational schema. `loadInto()` builds a plain `AppState` struct from SQL (no StateAPI mutators, no events, no ID conflicts), then calls `state.replaceState()` which atomically swaps state and fires one event. `saveFrom()` flushes state to SQL.

Database: `~/.config/performance/state.db`

Tables: `plugins` (with `is_instrument`), `presets` (with `kind`), `actions`, `songs`, `tracks` (with `color`, `source_type`), `busses`, `effects`, `sends`, `regions` (with `muted`, `quantize`), `takes`, `take_events`, `action_events` (with `track_id`), `bindings` (nullable `song_id` for global/song scope), `config`, `devices`, `device_controls`, `song_devices`.

### Identity

UUID everywhere. Every track, bus, effect, send has a UUID assigned at creation. Names are display properties only. All APIs, engine, GUI, and internal references use UUIDs. Names resolved to UUIDs once at Lua/Claude entry point.

### Rules for new code

- All state reads/writes go through StateAPI. Never call audioEngine for state.
- EngineAPI is for peak levels, processors, plugin UI, and plugin discovery only.
- EngineSync is the only code that calls AudioEngine write methods.
- NEVER use names as keys or identity. Use UUIDs.
- When adding a new settable value: add to StateModel, add StateAPI method, handle in EngineSync.

### Other Components

- **EngineSync** (`src/engine/EngineSync.h/.cpp`) — pure event subscriber. Subscribes to StateAPI events, applies to engine. Zero public methods. Handles bus/master `audioEnabled`.
- **AudioEngine** (`src/engine/AudioEngine.h/.mm`) — JUCE AudioProcessorGraph. All methods accept UUIDs. Pure view of state. Owns the `GraphWrapper` which wraps the graph for per-buffer MIDI scheduling.
- **GraphWrapper** (`src/engine/GraphWrapper.h`) — AudioProcessor wrapping the graph. Advances a sample-accurate beat clock, scans the `Arrangement` for MIDI events per buffer, routes to per-track `MidiSourceNode`s. Captures live MIDI to `RecordFIFO` when recording.
- **MidiSourceNode** (`src/engine/MidiSourceNode.h`) — per-track AudioProcessor for sequencer MIDI. Filled by GraphWrapper before graph processes, drained during processBlock. Connected to instrument alongside live MIDI input.
- **RecordFIFO** (`src/engine/RecordFIFO.h`) — lock-free SPSC ring buffer (1024 slots). Audio thread pushes beat-timestamped MIDI events, coordinator timer drains into Arrangement.
- **AudioRecordFIFO** (`src/engine/AudioRecordFIFO.h`) — lock-free SPSC ring buffer for audio samples (~5 sec at 48kHz stereo, interleaved floats). Audio thread pushes, writer thread drains.
- **AudioWriterThread** (`src/engine/AudioWriterThread.h`) — background thread draining AudioRecordFIFO to WAV file. De-interleaves and writes via JUCE AudioFormatWriter. Computes peaks during write for live waveform display.
- **AudioFileNode** (`src/engine/AudioFileNode.h`) — per-audio-track AudioProcessor for sequencer playback. Holds multiple loaded WAV files (one per region). GraphWrapper selects the active region by ID based on beat position. Converts beats to sample position using recordTempo.
- **MIDIEngine** (`src/engine/MIDIEngine.h/.cpp`) — MIDI input, forwards notes to audio graph, dispatches controls to SongRuntime. Supports device-specific monitoring and a global monitor (for debug pane). MIDI Learn with single-shot capture.
- **AutomationEngine** (`src/automation/AutomationEngine.h/.cpp`) — 60fps timer, interpolations with easing.
- **InternalSequencer** (`src/daw/InternalSequencer.h/.cpp`) — own transport, tempo, beat clock. Thread-safe atomics. Transport callback notifies coordinator on play/stop.
- **Arrangement** (`src/daw/Arrangement.h/.cpp`) — view over `TrackState.regions` in the current song. Provides `scanMidiEvents()` for playback, recording API (`startRecording`/`addRecordedEvent`/`stopRecording`), and region management (`moveRegion`, `duplicateRegion`, `removeRegion`). Does not own data — regions live in `TrackState`.
- **LuaEngine** (`src/scripting/LuaEngine.h/.cpp`) — embedded Lua via sol2. Takes StateAPI+EngineAPI+Coordinator.
- **IPCServer** (`src/ipc/IPCServer.h/.cpp`) — Unix domain socket `/tmp/performance.sock`. `bin/perf` shell command.
- **SongRuntime** (`src/song/SongRuntime.h/.cpp`) — MIDI control dispatch map.

### GUI

All GUI components take `StateAPI&` + `EngineAPI&` (no PerformanceAPI).

- **MainLayout** — root container: toolbar + sidebar + flexible dual-pane area + mixer. Left pane (ProducePane by default, also Debug or Mappings) and right pane (Chat or Logs) switchable via sidebar.
- **ProducePane** — DAW arrange view: Logic-style transport bar with LCD position display (BAR/BEAT/DIV/TICK + time + BPM + time sig), transport buttons (rewind/stop/play/record/cycle with active-state backgrounds), track headers with power icons and arm dots, timeline grid with regions. MIDI regions show mini piano roll; audio regions show waveform (sqrt-scaled peaks, live during recording). Action track shows 3D-ish spheres with duration tails, overlap-aware beehive lane layout. Regions are semi-transparent for overlap visibility, colored by track, darkened when track or region is muted. Click grid to set position (snaps to division). Drag track headers to reorder. Multi-track selection: click=select, Cmd+click=toggle, Shift+click=range. Region management: multi-select with Cmd+click, delete/backspace removes all selected, drag to move (horizontal + cross-track with snap-to-grid), option+drag to duplicate, Cmd+D to duplicate inline, Cmd+T to split at playhead, right-click for context menu (mute/unmute, delete, quantize, join). Region trimming: drag left/right edges with resize cursor, non-destructive. Auto-scroll: Logic-style page jump when playhead nears right edge; `ensurePlayheadVisible()` on all manual position changes. Two-finger horizontal scroll. Keyboard: space=play/stop, r=record, return=rewind, h/l=step by division, Shift+H/L=step by measure, Cmd+h/l/j/k=zoom.
- **MixerView** — track/bus/output strips, 30Hz poll (state for structure, engine for stereo peak levels). Drag track headers to reorder (blue indicator line, snaps to strip edges).
- **TrackStrip** — instrument slot (or input selector for audio input tracks), effect slots, output target selector (Master/No Output/Bus), sends panel, fader+stereo meters with IEC-scale dB labels. Power icon toggles `audioEnabled`. Red arm dot toggles `armed` for recording. Track preset callbacks from coordinator.
- **BusStrip** — effect slots, output target selector (Master/No Output/Bus), fader+stereo meters. Power icon toggles bus `audioEnabled`. Bus preset save/load via right-click menu.
- **OutputStrip** — master effect slots, master fader+stereo meters. Power icon toggles master `audioEnabled`.
- **FaderMeter** — fader + dual L/R meters on IEC-style non-linear dB scale (-60 to +6). Peak hold with exponential decay. Grid lines, color zones (green/amber/red at -12/0dB), dB tick labels. Fader handle center reaches full range (+6 to -60). Click-to-jump: clicking the fader track sets the fader to that position. Fader travel and meter bars share identical vertical bounds.
- **MusicalTyping** — on-screen keyboard (Cmd+K toggle). Computer keys mapped to MIDI notes (Logic layout). Octave shift (Z/X), velocity (C/V), sustain (Tab). Draggable floating panel. Injects MIDI via `audioEngine.injectMidi()`. Intercepts all keyboard input when active.
- **MorphEditor** — slot-based editor for compound morph actions. Growing list of action slots with inline action picker per slot. Parallel/sequential mode toggle. OK/Cancel with proper window close handling.
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
  midiInput ──────┐
  midiSourceNode ─┤→ instrument → [fx1 → fx2 →] ┬─ outputGain → masterGain
                                                   ├─ sendGain1  → Bus1
                                                   └─ sendGain2  → Bus2
  (midiInput = live controllers, midiSourceNode = sequencer playback)

Per audio input track:
  audioInput[ch] ─┐
  audioFileNode ──┤→ [fx1 → fx2 →] ┬─ outputGain → masterGain
                                     ├─ sendGain1  → Bus1
                                     └─ sendGain2  → Bus2
  (audioInput = live monitoring, audioFileNode = region playback)
  (mono inputs: channel duplicated to stereo; input optional for playback-only tracks)

Per bus:
  (summed sends) → [busFx1 → busFx2 →] busOutputGain → masterGain

Master output:
  masterGain → [masterFx1 → masterFx2 →] → audioOutput

GraphWrapper wraps the entire graph. In processBlock:
  1. Advances sample-accurate beat clock from sample count + tempo
  2. Scans Arrangement for MIDI events in this buffer's beat range
  3. Routes events to per-track MidiSourceNodes with exact sample offsets
  4. Captures incoming live MIDI to RecordFIFO when recording
  5. Flushes all notes (CC 123 + CC 120) on stop/seek (deferred via atomic flag)
  6. Delegates to graph.processBlock()
```

Audio device switching: `AudioEngine` implements `ChangeListener` on `AudioDeviceManager`. On device change, `rebuildGraph()` tears down IO nodes, reconfigures graph for new device's channel count, recreates IO nodes, rewires connections. If device goes null (mid-transition), processing stops cleanly and recovers on next notification. `InputMeter` callback provides per-channel peak levels for the debug pane.

Audio output and input devices are independent (macOS CoreAudio). Selection persists via `config["audio_output_device"]` and `config["audio_input_device"]` — restored on startup after `loadInto`. Only `outputDeviceName` or `inputDeviceName` is set (never both to the same value, which fails for output-only devices like MacBook Pro Speakers). Clicking the same device is a no-op. If device goes null mid-transition, async retry recovers once JUCE finishes opening the new device.

MIDI gating: disabled tracks (`audioEnabled=false`) receive no MIDI — prevents wasted synthesis across multiple instrument tracks. `midiEnabled` and `audioEnabled` are both required for MIDI connection in `rebuildConnections`. The power icon controls `audioEnabled` and also re-enables `midiEnabled` when turning a track on. Actions (`setActiveTrack`, `enableTrack`, `disableTrack`) set both `audioEnabled` and `midiEnabled` together so the UI reflects action-driven changes.

### Bindings & Actions

Bindings map MIDI controls to named actions with arguments. Two scopes: song-scoped (deleted with song) and global (always active). `effectiveBindings()` merges both (song wins on conflict). Bindings store action args as JSON arrays with track UUIDs (resolved from names at bind-time). Action `paramSchema` (JSON) defines expected parameters — used by MappingPane to generate appropriate input fields.

Built-in actions: `setActiveTrack(trackId)`, `enableTrack(trackId)`, `disableTrack(trackId)`, `fadeOut(trackId, duration, easing)`, `fadeIn(trackId, duration, easing)`, `crossfade(fromTrackId, toTrackId, duration, easing)`, `trackVolume(channelId)`, `morphToPreset(trackId, presetName, duration, easing)`, `morphChain(trackId, presetA, presetB, dwell, duration, easing)`, `morph({mode, actions[]})`. All track/channel args are UUIDs. `resolveTrack()` expects UUIDs only — no name fallback at runtime. `ActionInfo.durationParamIndex` explicitly identifies which arg is the duration (for UI duration bars).

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

103 tests:
- StateAPI tests (34): full in-memory state store coverage
- Persistence round-trip tests (3): save→load fidelity, multi-song, empty DB
- Integration tests (12): full coordinator→state→EngineSync→engine path
- EngineSync tests (23): mock engine verifying state→engine event dispatch (includes audio input tracks, audioEnabled, bus/master audioEnabled, instrument changes, song switching)
- Audio device config tests (4): device name persistence, config round-trip, audioEnabled persistence
- Sequencer tests (14): internal sequencer transport, tempo, beat position, loop, time signature
- Arrangement tests (13): region CRUD, MIDI event scanning, recording lifecycle, note capture

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
- Transport requires active audio device: beat clock runs in `GraphWrapper.processBlock`, so play/stop/position don't advance without an audio output device linked. Fix: fallback timer-based clock. Low priority — live use always has a device.

**Known issues with embedded Claude:**
- Bindings created via Claude/Lua may use wrong track names (case mismatch). GUI is more reliable.
- Custom action creation via Claude fails despite API working via direct IPC. Needs investigation (string escaping through chat→tool→IPC pipeline).

**Milestone: Sequencer + production tools complete (v0.0.1).**
MIDI + audio recording/playback, region management (select/move/copy/delete/mute), take folders, persistence, transport controls, auto-scroll, waveform display, multi-track recording, per-song tempo and time signature, preset morphing, metronome, flexible pane system, CC fader mapping.

**Recent additions:**
- Action track — one per song, auto-created. Beat-triggered actions on the timeline. 3D spheres with duration tails, overlap-aware beehive layout. Double-click to create, drag to reposition, right-click to edit/delete. Persisted in `action_events` table.
- Compound morph action — bundles multiple sub-actions (parallel or sequential). `morphChain` for preset A → dwell → preset B transitions. MorphEditor UI with growing action slots.
- Musical typing — on-screen keyboard (Cmd+K). Logic-style key mapping, octave/velocity controls, sustain, draggable panel.
- Track/region selection — multi-track (Cmd/Shift click), multi-region selection. Track selection highlights header + selects all regions.
- Region operations — non-destructive quantize (right-click submenu), trim (drag edges), split at playhead (Cmd+T), join selected regions.
- Smart grid snap — all playhead clicks, region drags, and trims snap to division boundaries via `snapBeatToGrid()`. Shift+H/L steps by measure.
- Cycle playback — drag ruler to set cycle region, 'c' to toggle, 'u' to set from selection. Draggable cycle edges with resize cursor. Loop wrapping in GraphWrapper with targeted note flush (per-note bitset tracking, inline flush before scan to avoid same-sample noteOff/noteOn race). Playhead jumps to cycle start on play.
- Ruler gutter — dedicated playhead-setting area with adaptive bar numbers and multi-resolution tick marks. Grid clicks deselect only.
- Note flush — targeted noteOff using per-channel bitset of active notes. Loop flush via MidiSourceNodes only (not live MIDI path, which would race with new noteOns).
- Fader improvements — click-to-jump, handle center reaches full +6/-60dB range, fader/meter vertical alignment.
- Preset name display — editor window shows correct preset name (resolved from state on open, updated on save/load).
- DB backup on every save (state.bak.db). Schema version tracking. Git tag v0.0.1.

**Feature backlog (high priority):**
- Undo/redo — snapshot-based, using existing `replaceState()`. Architecture is ready: AppState is fully copyable (all std containers, no pointers), replaceState fires one event, EngineSync rebuilds correctly. Implementation: `UndoHistory` with `deque<AppState>`, push before each user mutation, Cmd+Z restores. After restore: update arrangement pointer + reload audio files. Concerns: (1) mutation grouping (begin/end transaction for multi-step operations like track preset load), (2) audio recording undo needs WAV file cleanup side-channel, (3) morph undo needs automation cancellation, (4) cap at ~50 steps to limit memory (1-10MB per snapshot). NOT undoable: per-parameter tweaks, transport state, mid-recording mutations. ~50ms restore time for large songs.

**Feature backlog (near-term):**
- Atomic transport commands: InternalSequencer and GraphWrapper have independent beat clocks synced at 60Hz. Separate `setPlaybackBeatPosition` + `setPlaybackState` calls create race windows where the audio thread processes a buffer with partial state. Refactor to a single `startPlayback(beat, bpm, loop)` / `stopPlayback()` command that GraphWrapper reads atomically. Eliminates call-order bugs by construction.
- Stuck note prevention at region boundaries: `scanMidiEvents` should fire synthetic noteOffs at region end for unclosed notes. TODO marked in Arrangement.cpp.
- Customizable keyboard shortcuts — KeyBindings.h defaults → config overrides → runtime lookup.
- TempoMap + TimeSignatureMap — runtime evaluation of tempo/time-sig change events at specific beat positions. Currently one global value per song. Data model ready (vectors on SongState). No trapdoors.

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

~22,000 lines of source code (headers + implementation + tests). See `find src tests -name "*.h" -o -name "*.cpp" -o -name "*.mm" | xargs wc -l`.
