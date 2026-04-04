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

- **AudioEngine** — JUCE-based plugin hosting, linear signal chains, output mixing
- **MIDIEngine** — MIDI input handling, splits note vs control MIDI
- **EventBus** — typed event dispatch (MIDI input + internal events like transition-complete, clock tick)
- **Scheduler** — drives time-based routines frame-by-frame
- **Song** — plugin declarations, signal chains, control bindings, handler code
- **ControlMap** — typed abstraction of KeyLab's physical layout (pads, knobs, faders, buttons)
- **PluginParameter** — typed reference to a specific param on a specific plugin instance
- **Plugin UIs** — can open plugin GUIs for sound design; performance is all code-controlled

### Latency Budget

- Note events: sub-1ms (direct MIDI to plugin, limited by audio buffer size)
- Control/param changes: up to one buffer of latency (~2.9ms at 128 samples/44.1kHz)
- Buffer size target: 128 samples

### Pre-mortem (key risks)

1. **AU plugin compatibility** — plugins make assumptions about hosts. Mitigated by using JUCE which is battle-tested.
2. **Audio graph complexity** — mitigated by starting with linear chains only. Busses/sidechains are future work.
3. **Scope creep into DAW territory** — this is for live performance only. Production stays in Logic.

## Status

Architecture decided. Setting up JUCE/C++ development environment.
