# Live Performance Environment

A code-first runtime for live music performance on macOS. Solo performer, centered around an Arturia KeyLab 88 MkII and Audio Unit plugins. Think MainStage's live workflow with Max/MSP's flexibility, but expressed in code — no visual programming.

## Core Concepts

- **Song** — the top-level unit of performance. Defines which plugins are loaded, signal chains, all control mappings and behaviors. Reusable presets/functions can be pulled into any song.
- **Event-driven** — MIDI input (pads, knobs, faders, keys) triggers handler functions. Handlers have access to all exposed parameters of all instruments in the song.
- **Handlers as behaviors** — mappings aren't just value assignments. A pad press can kick off time-based interpolations, multi-param morphs, conditional logic — anything.
- **Library** — reusable handler functions, partial configurations, and presets that grow over time and get composed into songs.
- **Authoring model** — Claude writes and maintains all code, including song definitions. Will directs in natural language (e.g., "map this fader to reverb decay time").

## Architecture

**Language: C++ with JUCE framework** — JUCE handles AU plugin hosting, audio device management, MIDI I/O, and real-time audio rendering. Industry standard for audio apps. Eliminates the biggest technical risk (AU hosting from scratch).

### Two-domain design (like Max's message/signal split)

**Audio thread:**
- JUCE manages the audio callback and plugin rendering
- Linear signal chain per instrument: AU instrument → [effect 1 → effect 2 → ...] → output
- Note/performance MIDI goes straight to plugin MIDI input (fast path)
- Applies parameter changes from lock-free queue each callback

**Control thread:**
- Receives MIDI control events (pads, knobs, faders) via MIDI input
- Dispatches to song's event handlers (normal C++ code, no RT constraints)
- Drives the scheduler for time-based routines (interpolations, envelopes, sequences)
- Enqueues parameter writes to audio thread via lock-free ring buffer

### Major Components

**Implemented:**
- **AudioEngine** (`src/engine/AudioEngine.h/.mm`) — JUCE-based plugin hosting, named instrument chains (instrument → effects → output), plugin editor windows, third-party AU registration. Multiple chains mix to output in parallel. API: `createChain`, `addInstrument`, `addEffect`, `removeChain`, `clearAllChains`, `getInstrumentProcessor`, `getEffectProcessor`, `openPluginEditor`, `setChainMidiEnabled` — all by name. Plugin loading is async via `createPluginInstanceAsync` with optional `onLoaded` callback; `rebuildConnections` handles partially-loaded chains gracefully.
- **MIDIEngine** (`src/engine/MIDIEngine.h/.cpp`) — MIDI input from all devices, forwards all MIDI to audio graph, dispatches control events (CC, note on/off, pitch bend, pressure) to SongRuntime
- **SongDef** (`src/song/Song.h`) — declarative song definition: instruments, effect chains, control bindings with handler functions. MIDIControl struct identifies controls (CC/Note/PitchBend/Pressure). ControlHandler is `std::function<void(float)>` with 0-1 normalized value.
- **SongRuntime** (`src/song/SongRuntime.h/.cpp`) — loads a SongDef, creates named chains via AudioEngine, builds control dispatch map, routes MIDI control events to bound handlers. Auto-opens plugin editor UIs when instruments finish loading. `findParam` looks up by instrument/effect name. Supports wildcard channel matching (channel 0 = any).
- **Log** (`src/engine/Log.h/.cpp`) — `perfLog()` writes to both stderr and `/tmp/performance.log` for runtime observability without a connected terminal. `initLog()` at startup.
- **Plugin cache** — scan results cached to `~/.config/performance/plugin-cache.xml`. Eliminates ~30s+ AU scan on startup. Delete file to force rescan.

**Not yet implemented:**
- **EventBus** — typed event dispatch (MIDI input + internal events like transition-complete, clock tick)
- **Scheduler** — drives time-based routines frame-by-frame
- **ControlMap** — typed abstraction of KeyLab's physical layout (pads, knobs, faders, buttons)

### Latency Budget

- Note events: sub-1ms (direct MIDI to plugin, limited by audio buffer size)
- Control/param changes: up to one buffer of latency (~2.9ms at 128 samples/44.1kHz)
- Buffer size target: 128 samples (currently running at 512/48kHz = ~10.7ms due to default device settings)

### Third-party AU Plugin Loading

Modern macOS does not register third-party AU components via AudioComponentFindNext.
Our workaround: at startup, index .component bundles by reading Info.plist metadata
(no executable loading). When a plugin is requested, load its bundle, get the factory
function, and call AudioComponentRegister to make it visible to the AudioComponent system.
Bundles are kept alive for the process lifetime. This is transparent to the rest of the code.

### Pre-mortem (key risks)

1. **AU plugin compatibility** — plugins make assumptions about hosts. Mitigated by using JUCE which is battle-tested. Third-party AU registration solved via AudioComponentRegister workaround.
2. **Audio graph complexity** — mitigated by starting with linear chains only. Busses/sidechains are future work.
3. **Scope creep into DAW territory** — this is for live performance only. Production stays in Logic.

## Status

**Working:**
- JUCE/C++ project set up with CMake
- Audio device + MIDI input working
- AU plugin hosting working (JUCE AudioProcessorGraph)
- Third-party AU loading working (Keyscape, Kontakt, Massive X, Raum verified)
- Plugin editor UI windows auto-open on instrument load
- MIDI → plugin → audio output pipeline verified end-to-end
- Song model: SongDef, SongRuntime, MIDIEngine control dispatch all wired up
- Multi-instrument with per-chain MIDI enable/disable — pad switching verified
- Plugin cache for fast startup
- File-based logging (`perfLog`) for runtime observability

**Current state / next steps:**
- `main.cpp` loads a test song with Keyscape + Massive X, pad switching on channel 10 (note 36/37)
- Tested with MPK mini 3 (pads on ch10, keys on ch1)
- Note: Keyscape requires clicking through a splash screen and loading a preset before it produces sound
- EventBus, Scheduler, ControlMap not yet started
- No actual performance song definitions written yet — need a first real song
