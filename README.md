# Performance

A live music performance environment for macOS. MIDI controllers, live audio, Audio Unit plugins, and an embedded Claude instance for environment authoring.

![Screenshot](docs/screenshot.png)

## Overview

- Instrument tracks (MIDI to AU plugin) and audio input tracks (physical input to effects chain)
- Effects chains, busses, sends, per-track gain
- Stereo VU meters, IEC-style non-linear dB scale, peak hold with exponential decay
- MIDI device mapping with Learn mode
- Bindings: MIDI controls to named actions (fade, crossfade, track switch), song-scoped and global
- Custom Lua actions for multi-step transitions
- Independent audio input/output device selection, persisted
- Songs: named sessions with tracks, busses, bindings, scores. Persistent sandbox session.
- Embedded Claude with tool use for environment authoring via Lua
- SQLite persistence: tracks, instruments, effects, processor state, bindings, device mappings
- Sidebar (songs, library, maps, devices, panes), dual content panes, collapsible mixer

## Architecture

In-memory state store (StateAPI) as runtime SSOT. Event bus notifies engine sync layer, which applies changes to JUCE AudioProcessorGraph. SQLite for persistence.

Three APIs: StateAPI (state reads/writes), EngineAPI (peak levels, processors, plugin UI), PerformanceCoordinator (lifecycle, orchestration, action dispatch).

See [CLAUDE.md](CLAUDE.md) for details.

## Building

Requires macOS, Xcode command line tools, and CMake.

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Launch: `open Performance_artefacts/Debug/Performance.app`

## Testing

```bash
cd build
./PerformanceTests_artefacts/Debug/PerformanceTests
```

76 tests: state management, persistence round-trips, engine sync, audio device configuration.

## Tools

- `bin/perf` — send Lua commands to the running app via IPC
- `bin/midi-test` — send test MIDI notes via a virtual port
- `bin/midi-loop` — continuous MIDI note sending for testing
- `bin/reset` — reset all state (preserves device mappings)

## Tech stack

- **C++17** with JUCE framework
- **Audio Units** for plugin hosting
- **Lua** (sol2) for scripting and custom actions
- **SQLite** for persistence
- **Claude API** for embedded AI assistant

## Status

In development.
