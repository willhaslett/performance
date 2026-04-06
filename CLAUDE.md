# Live Performance Environment

A scriptable runtime for live music performance on macOS. Solo performer, centered around an Arturia KeyLab 88 MkII and Audio Unit plugins. The app is a live environment — always running, always ready. The registry (SQLite) is the single source of truth. The audio engine is a view of the registry.

## Core Concepts

- **The app is an environment** — it launches and restores its previous state from the registry. No explicit save needed to preserve your work. Tracks, instruments, effects, sends, gains, and snapshots all persist automatically.
- **Session** — there is always a current session (a song entity in the registry, named or unnamed). You can work without naming a song. `saveSong` gives the session a name. `loadSong` switches to a different one. On restart, the previous session is restored.
- **Song** — a named session. Lua scripts in `~/.config/performance/songs/` bootstrap songs (create tracks, busses, bindings), but the registry is the authoritative state.
- **Action-based bindings** — MIDI controls bind to named actions (e.g., `setActiveTrack`, `fadeOut`) with entity ID arguments. No inline code in bindings — all behavior is a registered, reusable action. Bindings persist in the registry and survive restart.
- **Automation** — `interpolate(from, to, duration, callback, easing)` with library helpers: `fadeOut`, `fadeIn`, `crossfade`, `paramSweep`.
- **Authoring model** — Claude runs embedded in the app (terminal emulator in the UI). Will plays and directs, Claude modifies the environment via the `perf` IPC command. The GUI provides direct manipulation. All consumers use the same API.

## Architecture

### Data Flow

```
Mutation (API call from Lua, Claude, GUI, IPC)
    ↓
Registry (SQLite — write)
    ↓
EngineSync.sync() (diff registry vs engine, apply changes)
    ↓
AudioEngine (audio graph matches registry)
```

One direction. One source of truth. The engine never has state that the registry doesn't know about. Sync is idempotent — call it as many times as you want.

### Components

- **PerformanceAPI** (`src/api/PerformanceAPI.h/.cpp`) — single interface for all consumers. Writes to registry, calls `engineSync->sync()`. Real-time values (gain) go direct to engine, persisted by 1Hz timer. Discrete state (MIDI enabled) writes to registry immediately. Action dispatcher (`executeAction`) resolves action names + entity ID args.
- **Registry** (`src/registry/Registry.h/.cpp`) — SQLite database (`~/.config/performance/registry.db`). Typed entities with UUIDs. Generic CRUD (`create`, `get`, `list`, `update`, `remove`) plus type-specific convenience methods. Emits events for UI updates.
- **RegistryEventBus** (`src/registry/RegistryEvents.h`) — pub/sub for UI components. Entity type constants in `EntityType` namespace.
- **EngineSync** (`src/engine/EngineSync.h/.cpp`) — reads the registry for a song, diffs against engine state, creates/removes what's needed. Order: busses → tracks → effects → sends. Also runs 1Hz persist timer writing engine values back to registry. Only creates new entities; never overwrites values on existing ones (engine owns live values after creation).
- **AudioEngine** (`src/engine/AudioEngine.h/.mm`) — JUCE AudioProcessorGraph, plugin hosting, Track/Bus DAG wiring, GainProcessor nodes. Never written to directly except for real-time values.
- **MIDIEngine** (`src/engine/MIDIEngine.h/.cpp`) — MIDI input from all devices, forwards note MIDI to audio graph, dispatches control events to SongRuntime.
- **AutomationEngine** (`src/automation/AutomationEngine.h/.cpp`) — 60fps timer, interpolations with easing functions.
- **LuaEngine** (`src/scripting/LuaEngine.h/.cpp`) — embedded Lua via sol2. Registers API as global functions. Loads library files from `~/.config/performance/lua_lib/`.
- **IPCServer** (`src/ipc/IPCServer.h/.cpp`) — Unix domain socket at `/tmp/performance.sock`. Accepts Lua strings, executes them, returns results.
- **SongRuntime** (`src/song/SongRuntime.h/.cpp`) — MIDI control dispatch map. Routes control events to bound handlers.

### GUI

- **MainLayout** (`src/gui/MainLayout.h/.cpp`) — root container: toolbar + sidebar + terminal + mixer
- **TerminalView** (`src/gui/TerminalView.h/.cpp`) — embedded terminal (libvterm) running Claude Code
- **MixerView** (`src/gui/MixerView.h/.cpp`) — track strips then bus strips, polls engine state at 30Hz
- **TrackStrip** (`src/gui/TrackStrip.h/.cpp`) — header with power icon + MIDI toggle, instrument slot, effect slots, fader+meter. Composes from PluginSlot and FaderMeter.
- **BusStrip** (`src/gui/BusStrip.h/.cpp`) — purple header, effect slots only, fader+meter. Same composition as TrackStrip minus instrument/MIDI.
- **PluginSlot** (`src/gui/PluginSlot.h/.cpp`) — reusable pill: plugin name, picker with snapshot submenu, right-click context menu (No Plugin / Replace). Works for both tracks and busses.
- **FaderMeter** (`src/gui/FaderMeter.h/.cpp`) — reusable fader + VU meter pair: dB scale, drag handling, peak level, color bands.
- **SendsPanel** (`src/gui/SendsPanel.h/.cpp`) — bottom-aligned panel in track strip. Logic-style horizontal rows: bus name pill + rotary knob (300° arc, 7:00–5:00) with signal glow. Dynamic height. Hidden when no busses exist.
- **Theme** (`src/gui/Theme.h`) — centralized colors, dimensions, corner radii, fonts. SSOT for visual consistency.
- **Sidebar** (`src/gui/Sidebar.h/.cpp`) — three sections: Songs (flat list), Library (instruments/effects with user snapshots), Actions (performance verbs with labels). Open by default. Subscribes to registry events.
- **RegistryTree** (`src/gui/RegistryTree.h/.cpp`) — collapsible tree with safe value-type rows
- **Divider** (`src/gui/Divider.h`) — draggable pane resizer, horizontal or vertical
- Modal keyboard: normal mode (s=sidebar, x=mixer, i=insert, Esc=close editor), insert mode sends keys to terminal
- Native macOS menu bar: File (New/Save/Load/Close Song), Track (New Instrument Track, New Effects Bus), View (Toggle Sidebar/Mixer)
- Resizable panes between sidebar/content and terminal/mixer

### Audio Graph

```
Per track:
  midiInput → instrument → [fx1 → fx2 →] ┬─ outputGain → audioOutput
                                           ├─ sendGain1  → Bus1
                                           └─ sendGain2  → Bus2
Per bus:
  (summed sends) → [busFx1 → busFx2 →] busOutputGain → audioOutput
```

Instrument switching is MIDI routing only — no graph rebuild, no pops.

### Registry Schema

Entities: `plugins`, `snapshots`, `songs` (with `initial_state` and `score` JSON columns), `tracks`, `busses`, `effects`, `sends`, `actions` (with `label` display name), `bindings`. All have UUID primary keys. Foreign keys with CASCADE deletes. Entity type constants in `EntityType` namespace.

Actions are performance verbs only — things you'd bind to MIDI controls: `setActiveTrack`, `enableTrack`, `disableTrack`, `fadeOut`, `fadeIn`, `crossfade`. Utility functions (log, openEditor, loadSong) are API calls, not registered actions.

Generic CRUD: `registryCreate(type, fields)`, `registryGet(id)`, `registryList(type, filters)`, `registryUpdate(id, fields)`, `registryDelete(id)` — all exposed to Lua.

### Song Model

A song consists of:
- **Initial state** — tracks, busses, sends, gains, MIDI routing, plugin snapshots, bindings. Saved explicitly by the user. What the song looks like when loaded fresh.
- **Score** — an ordered list of registered action references (action ID + args) representing the intended sequence of state changes during performance.

State at any point = initial state + replay of score actions 1..N.

There are no stored waypoints or intermediate state snapshots. Any intermediate state is computed by replaying the score from initial state. This guarantees consistency — if the score produces wrong state, you discover it during development (replay), not during performance.

**Score uses:**
- "Go to step N" for development — replays from initial state to work on any section
- Documentation of the performance — the performer knows what to trigger and when
- Reset — reload initial state (step 0)

**Persist timer** writes current engine state to the live registry tables for session restore (pick up where you left off on relaunch). The song's initial state is stored separately in the `initial_state` JSON column on the songs table — the persist timer never touches it.

**Operations:**
- `saveInitialState()` — captures current state (tracks, busses, effects, sends, gains, bindings, plugin snapshots) as the song's initial state
- `loadInitialState()` — restores the saved initial state, clearing live state and rebuilding the engine
- `saveScore(json)` / `getScore()` — persist and retrieve the action score

### Plugin State Snapshots

Stored per plugin in `~/.config/performance/snapshots/<pluginName>/<snapshotName>.state`. Binary blobs via JUCE `getStateInformation`/`setStateInformation`. Referenced by UUID in the registry. Independent of songs — any song/session can use any snapshot.

### IPC

`bin/perf` shell command sends Lua to the running app: `perf 'createTrack("Bass")'`. The embedded Claude uses this to control the app at runtime. `runtime/CLAUDE.md` is the prompt for the embedded Claude instance.

### Third-party AU Plugin Loading

Index .component bundle Info.plist metadata at startup, on-demand register via AudioComponentRegister. Plugin scan results cached to `~/.config/performance/plugin-cache.xml`.

### Device Maps

`devices/mpk_mini_3.lua` — MPK mini 3 control map. Pads in CC mode: bottom row CC 16-19 (pads 1-4), top row CC 20-23 (pads 5-8), channel 10. Keys on channel 1.

## Implementation Status

**Working:**
- Registry-driven engine: SQLite SSOT → EngineSync → AudioEngine
- Generic CRUD + pub/sub events on all entity types
- Track/Bus mixer DAG with sends, per-node gain, GainProcessor with peak metering
- AU plugin hosting with third-party loading and plugin cache
- Embedded terminal (libvterm) running Claude Code in the app
- IPC socket (`/tmp/performance.sock`) for live Lua execution via `bin/perf`
- Lua song scripts with automation library (`lua_lib/automation.lua`)
- Plugin state snapshots (save/restore, persisted in registry + disk)
- GUI: 3-pane layout (sidebar, terminal, mixer), toolbar, resizable panes with overlay dividers
- Mixer: TrackStrip (instrument slot + effect slots + fader + VU meter + power icon MIDI toggle) and BusStrip (effect slots + fader + VU meter, purple header)
- Reusable components: PluginSlot (pill with picker/context menu), FaderMeter (dB-scaled fader + VU pair)
- Plugin picker: instrument/effect filtered, submenu with snapshot selection, right-click for No Plugin / Replace
- Theme system (Theme.h) — all colors, dimensions, fonts centralized
- Sidebar: Songs (flat), Library (instruments/effects with user snapshots), Actions (performance verbs with labels)
- Instrument switching via MIDI routing (no graph rebuild, no pops)
- Automation engine with easing functions (linear, easein, easeout, cosine, scurve)
- Global keyboard shortcuts via NSEvent monitor (modal: normal/insert)
- Native macOS menu bar: File, Track (New Instrument Track, New Effects Bus), View
- Session restore from registry on app launch (no Lua re-execution needed)
- Action-based bindings with entity ID arguments, persisted in registry
- Song initial state: save/load checkpoint separate from live state
- Score: ordered action list with replay ("go to step N")
- Default unnamed session on first run (no Lua bootstrap required)
- 1Hz persist timer: engine values (gain, MIDI enabled) written back to registry
- Python MIDI test tool (`tools/send_notes.py`) for testing without hardware

**Working (continued):**
- SendsPanel: Logic-style pill+knob rows with signal glow, dynamic height
- IPC table return serialization (Lua tables → JSON over socket for queries like `registryList`)
- Content-driven mixer height: mixer pane grows/shrinks based on tallest strip
- Remove bus effect via "No Plugin" context menu

**TODOs:**
- Track presets (save/load a full track configuration)
- Undo/redo via registry history table (log old/new values per mutation, walk backward/forward)
- MIDI device hot-plug
- MIDI effects (transpose, channel filter, arpeggiator)
- Audio device configuration (buffer size, sample rate)
- Plugin load performance (AU plugins block message thread during instantiation — progress indicator or background process)
- Track header: click to select (future track selection concept)
- Delete track / delete bus from GUI
- Insert mode swallows all key events including Cmd+Q — need to pass through system shortcuts
- Fader/knob drag: value stops changing at screen edge — need unbounded drag without cursor glitches
