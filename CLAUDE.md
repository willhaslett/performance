# Live Performance Environment

A scriptable runtime for live music performance on macOS. Solo performer, centered around an Arturia KeyLab 88 MkII and Audio Unit plugins. The app is a live environment — an always-running audio host with a runtime API that Claude, the GUI, and saved songs all program against.

## Core Concepts

- **The app is an environment** — it launches, initializes audio/MIDI, and waits. Songs, tracks, plugins, and mappings are created and modified at runtime through the API. Recompilation is only needed to add new engine capabilities.
- **Song** — setup + mappings. Setup defines the tracks, busses, effects, sends, and initial configuration. Mappings bind MIDI events to handler functions. There is no scene/state machine — the state is simply the current state of the engine as shaped by whatever handlers have fired.
- **Event-driven** — MIDI input (pads, knobs, faders, keys) triggers handler functions. Handlers can do anything: set a parameter, launch a multi-parameter interpolation, swap instruments, reset the song to its initial state.
- **Handlers as behaviors** — mappings aren't just value assignments. A pad press can kick off time-based interpolations, multi-param morphs, conditional logic — anything expressible in code.
- **Library** — reusable handler functions, partial configurations, and presets that grow over time and get composed into songs.
- **Authoring model** — Claude and Will collaborate at runtime. Will plays and directs ("map that fader to filter cutoff"), Claude writes and executes API calls and Lua scripts live. The GUI provides direct manipulation for common operations. All three use the same API.

## Architecture

### Layers

```
┌─────────────────────────────────────────────────┐
│  Consumers                                      │
│  ┌─────┐  ┌───────┐  ┌──────┐  ┌────────────┐  │
│  │ GUI │  │ Claude │  │ Lua  │  │ Song files │  │
│  └──┬──┘  └───┬───┘  └──┬───┘  └─────┬──────┘  │
│     └─────────┴─────────┴─────────────┘         │
│                     │                            │
│              ┌──────▼──────┐                     │
│              │     API     │  ← single interface │
│              └──────┬──────┘    for all mutation  │
│              ┌──────▼──────┐                     │
│              │   Engine    │  ← C++/JUCE         │
│              └─────────────┘                     │
└─────────────────────────────────────────────────┘
```

- **Engine** (C++) — audio graph, plugin hosting, MIDI routing. Low-level. Only changes when we need new capabilities.
- **API** — the public interface to the engine. All state changes go through here. High-level operations: `createTrack`, `loadInstrument`, `addEffect`, `addSend`, `setGain`, `bindControl`, `saveSong`, `loadSong`, etc.
- **Consumers** — all equal peers calling the same API:
  - **GUI** — conventional app interface for browsing plugins, adjusting mix, managing songs
  - **Claude** — issues API calls at runtime via IPC (likely local socket + JSON). Can compose complex behaviors, build new handler functions, author songs live during creative sessions.
  - **Lua** — embedded scripting for handler functions that need logic (interpolations, conditionals, sequences). Hot-reloadable.
  - **Song files** — persisted songs. Loading = executing a sequence of API calls that recreate the configuration.

### Data Model

#### Track
- **name** — user-assigned (e.g., "Keys", "Bass")
- **instrument** — one AU plugin
- **midiEffects[]** — ordered, our own code (transpose, arpeggiator, channel filter). Not AU plugins.
- **audioEffects[]** — ordered AU insert effects (reverb, delay, EQ)
- **sends[]** — list of (busName, gain). Post-insert sends.
- **outputGain** — direct out level. 0.0 = muted dry (send-only). Default 1.0.
- **midiEnabled** — receives note MIDI or not

#### Bus
- **name** — user-assigned (e.g., "ReverbBus")
- **audioEffects[]** — ordered AU effects
- **outputGain** — bus master level. Default 1.0.
- Routes to main output (no bus-to-bus chaining for now)

#### GainProcessor
- Trivial AudioProcessor: `buffer.applyGain(gain)` with `std::atomic<float>`
- One per track direct out, one per send, one per bus out
- Gain changes are real-time safe (atomic), no graph rebuild needed

#### Song
- **name**
- **setup** — tracks, busses, effects, sends, initial gains
- **mappings** — MIDI event → handler function

No scenes, no state machine. "Go to the intro" is just a handler that sets parameters to specific values. The only state is the live state of the engine.

### Audio Graph (JUCE AudioProcessorGraph)

The graph is a DAG, not just linear chains:

```
Per track:
  midiInput → [midiFx1 → midiFx2 →] instrument → [fx1 → fx2 →] ┬─ outputGain → audioOutput
                                                                   ├─ sendGain1  → Bus1
                                                                   └─ sendGain2  → Bus2
Per bus:
  (summed sends) → [busFx1 → busFx2 →] busOutputGain → audioOutput
```

Multiple sends to the same bus = automatic summing in the JUCE graph. A track can send to multiple busses AND direct out simultaneously, each with independent gain.

### MIDI Effects

MIDI effects are our own C++ code, not AU plugins. They process `MidiBuffer` in-place and are wrapped as `AudioProcessor` subclasses to live in the graph. Chain: midiInput → midiEffect1 → midiEffect2 → instrument.

Examples: transpose, channel filter, arpeggiator, chord generator, note filter.

### Two-domain design (like Max's message/signal split)

**Audio thread:**
- JUCE manages the audio callback and plugin rendering
- Processes the DAG: tracks → insert effects → sends/direct out → busses → output
- Note/performance MIDI goes through MIDI effect chain to plugin input
- GainProcessor nodes apply gain changes from atomic values each callback

**Control thread:**
- Receives MIDI control events (pads, knobs, faders) via MIDI input
- Dispatches to handler functions (normal C++ or Lua, no RT constraints)
- Drives the scheduler for time-based routines (interpolations, envelopes)
- Writes parameter/gain changes via atomic values or lock-free queue

### Claude ↔ App Communication

Claude connects to the running app via local socket (IPC). Sends JSON-encoded API calls, receives responses. This allows Claude to:
- Issue API commands at runtime ("create a track", "bind this control")
- Query state ("what tracks are loaded", "what's the current gain on the reverb bus")
- Write and hot-reload Lua handler functions
- Compose complex behaviors without recompilation

### Third-party AU Plugin Loading

Modern macOS does not register third-party AU components via AudioComponentFindNext.
Our workaround: at startup, index .component bundles by reading Info.plist metadata
(no executable loading). When a plugin is requested, load its bundle, get the factory
function, and call AudioComponentRegister to make it visible to the AudioComponent system.
Bundles are kept alive for the process lifetime. This is transparent to the rest of the code.

### Latency Budget

- Note events: sub-1ms (direct MIDI to plugin, limited by audio buffer size)
- Control/param changes: up to one buffer of latency (~2.9ms at 128 samples/44.1kHz)
- Buffer size target: 128 samples (currently running at 512/48kHz = ~10.7ms due to default device settings)

### Pre-mortem (key risks)

1. **AU plugin compatibility** — plugins make assumptions about hosts. Mitigated by using JUCE which is battle-tested. Third-party AU registration solved via AudioComponentRegister workaround.
2. **Audio graph complexity** — DAG with tracks, busses, and sends. Mitigated by building incrementally, testing at each step.
3. **Scope creep into DAW territory** — no timeline, no recording, no arrangement. This is a live performance and sound design environment. Production stays in Logic.
4. **IPC/Lua complexity** — mitigated by building the API layer first with direct C++ calls, then adding IPC and Lua as transport layers on top.

## Implementation Status

**Working:**
- JUCE/C++ project set up with CMake
- Audio device + MIDI input working
- AU plugin hosting working (JUCE AudioProcessorGraph)
- Third-party AU loading working (Keyscape, Kontakt, Massive X, Raum verified)
- Plugin editor UI windows auto-open on instrument load
- MIDI → plugin → audio output pipeline verified end-to-end
- Multi-instrument with per-chain MIDI enable/disable — pad switching verified
- Plugin cache (`~/.config/performance/plugin-cache.xml`) for fast startup
- File-based logging (`perfLog` → `/tmp/performance.log`)

**Current engine (pre-refactor):**
- AudioEngine uses flat `InstrumentChain` structs (instrument + effects → output)
- No busses, sends, or gain nodes
- SongDef/SongRuntime with control dispatch (CC, note, pitch bend, pressure)
- `main.cpp` loads a hardcoded test song — needs to be replaced by the API/song system

**Next: Phase 1 — Track/Bus data model + API layer**
- GainProcessor
- Track/Bus structs replacing InstrumentChain
- DAG wiring in rebuildConnections (sends, busses, per-node gain)
- API class wrapping AudioEngine
- Test with a song that has a reverb bus + sends

**Future phases:**
- MIDI effects (transpose, channel filter, arpeggiator)
- Lua embedding for handler functions
- IPC (local socket) for Claude ↔ app communication
- Song persistence (save/load)
- Scheduler for time-based routines (interpolations, envelopes)
- GUI
