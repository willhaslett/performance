# Live Performance Environment

A scriptable runtime for live music performance on macOS. Solo performer, centered around an Arturia KeyLab 88 MkII and Audio Unit plugins. The app is a live environment — an always-running audio host with a runtime API that Claude, the GUI, and Lua songs all program against.

## Core Concepts

- **The app is an environment** — it launches, initializes audio/MIDI, and waits. Songs, tracks, plugins, and mappings are created and modified at runtime through the API. Recompilation is only needed to add new engine capabilities.
- **Song** — a Lua script that calls API functions to set up tracks, busses, effects, sends, and bind MIDI events to handlers. Lives in `~/.config/performance/songs/`.
- **Event-driven** — MIDI input (pads, knobs, faders, keys) triggers Lua handler functions. Handlers can do anything: set a parameter, launch interpolations, swap instruments.
- **Automation** — time-based behaviors built on `interpolate(from, to, duration, callback, easing)`. Library functions like `fadeOut`, `fadeIn`, `crossfade`, `paramSweep` compose on top.
- **Authoring model** — Claude and Will collaborate at runtime. Will plays and directs, Claude writes Lua scripts and API calls. The GUI (future) provides direct manipulation. All consumers use the same API.

## Architecture

### Layers

```
┌─────────────────────────────────────────────────┐
│  Consumers                                      │
│  ┌─────┐  ┌───────┐  ┌──────────────────────┐  │
│  │ GUI │  │ Claude │  │ Lua (songs + lib)    │  │
│  └──┬──┘  └───┬───┘  └──────────┬───────────┘  │
│     └─────────┴─────────────────┘               │
│                     │                            │
│              ┌──────▼──────┐                     │
│              │ Performance │  ← single interface │
│              │     API     │    for all mutation  │
│              └──────┬──────┘                     │
│         ┌──────────┬┴──────────┐                 │
│    ┌────▼────┐ ┌───▼────┐ ┌───▼──────────┐      │
│    │  Audio  │ │  MIDI  │ │  Automation  │      │
│    │ Engine  │ │ Engine │ │    Engine    │      │
│    └─────────┘ └────────┘ └─────────────┘      │
└─────────────────────────────────────────────────┘
```

- **PerformanceAPI** (`src/api/PerformanceAPI.h/.cpp`) — owns AudioEngine, MIDIEngine, AutomationEngine, SongRuntime. Single interface for all consumers. Exposes: track/bus/send CRUD, parameter get/set, MIDI control bind/unbind, automation (interpolate/delay/cancel), plugin editor management, logging.
- **AudioEngine** (`src/engine/AudioEngine.h/.mm`) — JUCE AudioProcessorGraph, plugin hosting, Track/Bus DAG wiring, GainProcessor nodes. Handles async plugin loading, third-party AU registration, plugin cache.
- **MIDIEngine** (`src/engine/MIDIEngine.h/.cpp`) — MIDI input from all devices, forwards note MIDI to audio graph, dispatches control events to SongRuntime.
- **AutomationEngine** (`src/automation/AutomationEngine.h/.cpp`) — JUCE Timer ticking at 60fps, manages running interpolations with easing functions. Built-in easings: linear, easein, easeout, cosine, scurve. Also accepts custom easing functions from Lua.
- **LuaEngine** (`src/scripting/LuaEngine.h/.cpp`) — embedded Lua via sol2. Registers all PerformanceAPI methods as global Lua functions. Loads Lua library files from `~/.config/performance/lua_lib/` before songs. Manages song loading/unloading.
- **SongRuntime** (`src/song/SongRuntime.h/.cpp`) — internal to the API. Manages MIDI control dispatch map. Routes control events to bound handlers. Supports wildcard channel matching (channel 0 = any).

### Data Model

#### Track
- One AU instrument, ordered audio insert effects, sends to busses, output gain, MIDI enable flag
- MIDI effects (transpose, arpeggiator) planned but not yet implemented

#### Bus
- Ordered AU audio effects, output gain. Receives summed audio from track sends. Routes to main output.

#### GainProcessor (`src/engine/GainProcessor.h`)
- `std::atomic<float>` gain, real-time safe. One per track output, per send, per bus output.

### Audio Graph

```
Per track:
  midiInput → instrument → [fx1 → fx2 →] ┬─ outputGain → audioOutput
                                           ├─ sendGain1  → Bus1
                                           └─ sendGain2  → Bus2
Per bus:
  (summed sends) → [busFx1 → busFx2 →] busOutputGain → audioOutput
```

Instrument switching is MIDI routing only — connect/disconnect MIDI to the instrument node. The audio signal path stays static during performance (no graph rebuild = no pops).

### Song Format (Lua)

Songs are `.lua` files in `~/.config/performance/songs/`:

```lua
song("My Song")
createBus("Reverb")
addBusEffect("Reverb", "Hall", "Raum")
createTrack("Keys")
addInstrument("Keys", "Keyscape")
addSend("Keys", "Reverb", 0.3)

bind("note", 10, 36, function(val)
    if val == 0 then return end
    fadeOut("Keys", 3.0, "cosine")
end, "Pad 1 -> Fade Out")
```

### Lua Library (`~/.config/performance/lua_lib/`)

Lua files loaded automatically before songs. Provides automation helpers:
- `fadeOut(track, duration, easing)`, `fadeIn(track, duration, easing)`
- `fadeTo(track, target, duration, easing)`, `crossfade(from, to, duration, easing)`
- `paramSweep(track, param, from, to, duration, easing)`

### Third-party AU Plugin Loading

Modern macOS does not register third-party AU components via AudioComponentFindNext.
Workaround: index .component bundle Info.plist metadata at startup, on-demand load and register via AudioComponentRegister when requested. Transparent to the rest of the code.

### Plugin Cache

Scan results cached to `~/.config/performance/plugin-cache.xml`. Eliminates ~30s+ AU scan on startup. Delete file to force rescan.

### Logging

`perfLog()` writes to both stderr and `/tmp/performance.log`. All components use tagged prefixes: `[App]`, `[Engine]`, `[MIDI]`, `[Song]`, `[Lua]`, `[Automation]`, `[API]`.

### Latency Budget

- Note events: sub-1ms (direct MIDI to plugin, limited by audio buffer size)
- Control/param changes: up to one buffer of latency
- Buffer size target: 128 samples (currently running at 512/48kHz = ~10.7ms due to default device settings)

## Implementation Status

**Working:**
- JUCE/C++ project with CMake, Lua 5.4 + sol2 via FetchContent
- AU plugin hosting with third-party loading (Keyscape, Kontakt, Massive X, Raum verified)
- Track/Bus mixer DAG with sends and per-node gain
- Plugin editor UIs auto-open on instrument load
- Instrument switching via MIDI routing (no graph rebuild, no pops)
- Plugin cache for fast startup
- PerformanceAPI layer — all consumers go through single interface
- Lua song scripts — songs are .lua files, no recompile needed
- Automation engine — interpolate with easing functions, cancellable
- Lua automation library — fadeOut, fadeIn, crossfade, paramSweep
- Tested with MPK mini 3 (pads on ch10, keys on ch1)

**Known issues:**
- Occasional stuck notes on instrument switch
- Keyscape requires clicking through splash screen and loading a preset before it produces sound
- Audio device settings are hardcoded defaults (512 samples, 48kHz)

**Next steps:**
- IPC (local socket) for Claude ↔ app communication at runtime
- MIDI effects (transpose, channel filter, arpeggiator)
- Song management from Lua/API (list, switch songs at runtime)
- Audio device configuration
- GUI
