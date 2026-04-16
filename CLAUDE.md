# Live Performance Environment

A scriptable runtime for live music performance on macOS. Solo performer, centered around an Arturia KeyLab 88 MkII and Audio Unit plugins. The app is a live environment — always running, always ready. An in-memory state store is the single source of truth at runtime. SQLite is the persistence layer (load on startup, save on demand). The audio engine is a pure view of state.

> Changelog, completed work, test inventory, and known issues live in `DEV_HISTORY.md`. Forward-looking DAW bridge design lives in `docs/DAW_BRIDGE_PLAN.md`. Authoritative history is `git log`.

## Version & Distribution

**Current version: `0.0.1`** — SSOT is `CMakeLists.txt` line 2: `project(Performance VERSION 0.0.1)`. `0.1.0` will be the first beta.

**Build pipeline:** `scripts/build-release.sh [version]` — one command for Release build → code sign → DMG → notarize → staple. Version defaults to the CMake version. Output: `dist/Performance-<version>.dmg`. Requires Apple Developer ID certificate + keychain-stored notarization credentials (setup documented in the script).

**Beta expiry:** compiled-in date check in `main.mm` — currently October 16, 2026 (6 months). Shows dialog and quits if expired. Update the `juce::Time` constructor for each release cycle.

**Binary:** Universal (arm64 + x86_64), deployment target macOS 11.0 (Big Sur). Covers every Mac still receiving security updates.

**Toolbar build info:** commit hash shown right-aligned in the toolbar (`textDim`, `fontSizeSm`, selectable/copyable TextEditor). When the commit is tagged (e.g., `git tag v0.0.1`), the tag appears alongside the hash. Reconfigure CMake (`cmake -S . -B build`) to pick up new git state.

**Theme system:** see Theme section below. Factory themes (`minimal_dark`, `minimal_light`) baked into binary via `juce_add_binary_data` from `runtime/themes/*.json`. User themes in `~/.config/performance/themes/` override factory on id collision. Active theme: `config["active_theme"]`, defaults to `"minimal_dark"`.

**Context menus:** `PerformanceLookAndFeel` (`src/gui/PerformanceLookAndFeel.h/.cpp`) set as app-wide default at startup. Styles all JUCE-drawn popup menus, combo box dropdowns, scroll bars, and document window chrome using Theme tokens. Native macOS menu bar is unaffected (drawn by OS).

### Pre-beta checklist (for first-friend distribution)

- [x] Code signing + notarization pipeline
- [x] Beta expiry check
- [x] Universal binary (arm64 + x86_64)
- [x] Build info in toolbar (commit hash + tagged version)
- [x] Themed context menus via LookAndFeel
- [ ] First-run audio device auto-selection (currently requires manual Settings → Output Device)
- [ ] Failed plugin load feedback — status indicator on the slot when instantiation fails (currently silent)
- [ ] Getting started doc — keyboard shortcuts cheat sheet, audio setup, basic workflow
- [ ] "Show Log File" menu item — so testers can find `/tmp/performance.log` to report crashes
- [ ] Feedback channel — email, form, or built-in "Report Issue" link

## Active Work

### GUI architecture — current state

The Performer view is now split into **two independent pane content types**: `ControllersPane` (device tree, learn mode, MIDI control rows) and `SongMappingsPane` (Atemporal + Score sections). Both can be placed in any Left/Right slot. `⌘U` toggles the pair together (Left=Controllers, Right=SongMappings). Cross-pane drag uses JUCE's `DragAndDropContainer` (rooted at `MainLayout`) / `DragAndDropTarget` pattern. MainLayout also installs a single global MIDI monitor and dispatches to both panes for activity dots.

**Per-content preferred widths** are in: swapping a content type into the Left slot snaps the divider to that content's preferred proportion (`Controllers = 0.28`, `SongMappings = 0.55`, `Produce = 0.65`, etc.). No user-drag divider yet.

**File → Open Song ▸** cascading submenu with live menu invalidation. macOS menu cache refreshes via `menuItemsChanged()` on Song/Config state events.

### Theme system

Runtime-mutable. Every token in `Theme.h` (colors, fonts, spacing, dimensions, radii — 80+ values) is `inline` non-const and loadable from JSON at startup. Call sites are unchanged: `Theme::color(Theme::Color::bgApp)`, `Theme::headerHeight`, etc.

**Multi-source:**
- **Factory themes** compiled into the binary via `juce_add_binary_data` from `runtime/themes/*.json`. The JSON files are first-class files in the repo — edit one, rebuild, it's baked in. Adding a factory theme = drop a `.json` + one line in `CMakeLists.txt`.
- **User themes** scanned from `~/.config/performance/themes/*.json`. User overrides factory on id collision (copy a factory theme to the user dir and customize).
- `Theme::availableThemes()` returns the merged list; `Theme::loadThemeById(id)` resolves across both sources (user first, then factory).
- `config["active_theme"]` stores the active theme id. Defaults to `"minimal_dark"`.
- Two factory themes exist: `minimal_dark` (the default) and `minimal_light` (first-pass light mode, functional, ready for iteration).

**Theme file format:** See `runtime/themes/minimal_dark.json` for the full schema — every token is there. Colors use `#RRGGBB` (opaque) or `#AARRGGBB` (with alpha). Partial overrides allowed — missing keys stay at current values.

**Rules for GUI code** (unchanged from theming sweep): no raw hex colors, no raw font sizes, no magic spacing — everything through `Theme.h` tokens. Details in the Theme section below.

### Architectural direction (decided, not fully implemented)

The authoring model: **chat with Claude is the primary creation surface; every other pane is a result surface.** No pane is always open, including chat. The app serves **Performance** and **Production** — two modes of creation. Full rationale in the `project_authoring_model` memory.

**What's done:**
- [x] MappingPane split into `ControllersPane` + `SongMappingsPane` with JUCE drag-and-drop.
- [x] Per-content preferred widths.
- [x] File → Open Song submenu.
- [x] Runtime-mutable theme system with factory + user themes.

**What's next (roughly ordered):**
- **⌘O Songs palette** — modal overlay, type-to-filter, Enter to load, Esc to dismiss. Performance-time song switching that doesn't require the sidebar.
- **Sidebar-as-slot refactor** — dissolve tabs, make sidebar a flexible pane slot like Left/Right/Mixer. Now less urgent since Controllers is already its own pane. Still a cleanup win.
- **Theme picker UI** — menu or settings entry to switch themes. `availableThemes()` is ready; just needs the UI.
- **Layout presets** (named configurations toggled by shortcut) — deferred until underlying capabilities are in place.

### Theming sweep status

**Done (token-clean, no hardcoded colors/fonts):**
- [x] `ProducePane` — paint fixes, pill hover, shared track-name height.
- [x] `MixerView` family (`TrackStrip`, `BusStrip`, `OutputStrip`, `FaderMeter`, `SendsPanel`, `PluginSlot`).
- [x] `ControllersPane` — `bgSurface` container lift, activity dots tokenized.
- [x] `SongMappingsPane` — font sizes at Lg for Performer-as-first-class, semantic row backgrounds.

**Not yet swept (hardcoded color/font literals remain):**
- `Sidebar`, `DebugPane`, `LogPane`, `ChatView`, `SettingsWindow`, `MusicalTyping`, `MorphEditor`, `KeyBindingEditor`, `SaveAsDialog`, `MainLayout` overlay.
- These panes still function correctly under theme switching — they just won't fully respond to color/font changes in the theme file until swept. Sweep them as a cleanup pass when focus returns to visual polish.

### Smaller design questions still open

- **Fader handle shape** — currently a rounded rect with center groove; considering a more physical cap.
- **Selected track vs region contrast** — `bgSelection` (`0x262626`) vs `bgSurfaceRaised` (`0x333333`). Verify distinguishability.
- **Remove `bgRecessed` if unused** — meter grooves now use `bgSlot`. Grep and delete if nothing references it.
- **SongMappingsPane hover affordances** — `[+]` add buttons, group field, disclosure triangles lack explicit hover states.
- **Liveliness of sparse panes** — Controllers lift to `bgSurface` works (no `bgControl` rows inside). For Atemporal/Score the vocabulary conflict with mixer strips is unresolved. Don't retry surface-only lifts without addressing the collision first.

## Backlog

Deferred but tracked. Pull from here when picking up new work.

**High priority:**
- **LCD interactivity** — all LCD values should support drag-to-change and double-click-to-edit. Currently only BPM and time sig are editable; BAR/BEAT/DIV/TICK and time display are read-only.
- **Stuck note prevention at region boundaries** — `scanMidiEvents` should fire synthetic noteOffs at region end for unclosed notes. TODO marked in `Arrangement.cpp`.
- **TempoMap + TimeSignatureMap** — runtime evaluation of tempo/time-sig change events at specific beat positions. Data model ready (vectors on `SongState`); currently one global value per song. No trapdoors.

**Longer-term:**
- **Refactor oversized GUI files** — `ProducePane.cpp` is 2520 lines (header painting, grid, regions, mouse, keyboard all in one). Check `MainLayout` and `Sidebar` too. *Defer until the theming sweep wraps* to avoid churn with in-flight visual changes.
- MIDI effects (transpose, channel filter, arpeggiator).
- Fader/knob drag: value stops changing at screen edge.
- Background plugin state capture: move `getStateInformation` calls off the message thread (root cause of the beach ball; hard — JUCE plugin APIs aren't thread-safe).
- Failed plugin load feedback: status indicator on the slot when instantiation fails.
- Settings window MIDI tab content (channel filtering, transpose, etc).

## Core Concepts

- **The app is an environment** — launches and restores its previous state from SQLite into the in-memory state store. Saves on quit and on explicit save.
- **Sandbox** — the permanent scratchpad session. Always exists, undeletable, top of the sidebar.
- **Song** — a named session with its own tracks, busses, sends, bindings. Switching songs clears the engine and rebuilds from state.
- **Bindings** — MIDI controls bind to named actions with entity ID arguments. Two scopes: global (always active) and song-scoped. *In practice, all current bindings are song-scoped.*
- **Score** — an ordered list of action references per song. Used for development ("go to step N" replays from initial state) and as documentation of performance transitions.
- **Automation** — `interpolate(from, to, duration, callback, easing)` and `delay(seconds, callback)` with actions: `fadeOut`, `fadeIn`, `crossfade`, `morphToPreset`, `morphChain`, `morph` (compound bundling parallel/sequential sub-actions).
- **Authoring model** — Claude runs embedded in the app (native chat UI, Claude API with tool use). Will plays and directs, Claude modifies the environment via the `perf` tool (Lua execution). The GUI provides direct manipulation. All consumers use the same APIs.

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
- **EngineAPI** (`src/api/EngineAPI.h/.cpp`) — engine-only concerns: peak levels, processor access (for presets/params), plugin editor windows, plugin discovery. Use only when the state store can't provide it.
- **PerformanceCoordinator** (`src/api/PerformanceCoordinator.h/.cpp`) — lifecycle and orchestration: init/shutdown, song management, track presets, automation, action dispatch. Owns all subsystems, exposes `state()` and `engine()`.

### State Model (`src/state/StateModel.h`)

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

Key model facts:
- Track source types: `Instrument` (MIDI→plugin→audio), `AudioInput` (physical input→fx→output, mono→stereo upmix), `Action` (beat-triggered, no audio, hidden from mixer).
- `midiEnabled` and `audioEnabled` are distinct; both required for MIDI routing. The power icon controls `audioEnabled` and re-enables `midiEnabled` on power-on.
- `armed`, `muted`, `soloed`, `recordModeActive` — runtime, not persisted. Recording is explicit: armed tracks record only when record mode is active.
- Regions are take folders. `MidiEventState` is raw events; notes derived via `buildNoteList()`.
- `RegionState.quantize` — non-destructive, applied at playback/display.
- Action track: one per song (auto-created), no regions — events stored directly with absolute beat positions.
- Multi-region and multi-track selection with Cmd/Shift modifiers.

### Identity

UUID everywhere. Every track, bus, effect, send has a UUID at creation. Names are display-only. All APIs, engine, GUI, and internal references use UUIDs. Names resolve to UUIDs once at Lua/Claude entry.

### Rules for new code

- All state reads/writes go through StateAPI. Never call audioEngine for state.
- EngineAPI is for peak levels, processors, plugin UI, and plugin discovery only.
- EngineSync is the only code that calls AudioEngine write methods.
- NEVER use names as keys or identity. Use UUIDs.
- When adding a new settable value: add to StateModel, add StateAPI method, handle in EngineSync.
- Fail loud: use `PERF_ASSERT` and the asserting helpers `song()`/`track()`/`bus()`/`device()` in StateAPI. Don't paper over programming errors.

### Persistence

`src/persistence/PersistenceLayer.h/.cpp`. SQLite normalized schema at `~/.config/performance/state.db` (with `state.bak.db` backup on each save). `loadInto()` builds a plain `AppState` then calls `state.replaceState()` (atomic swap, one event). Schema version tracked. *No migration shims at this stage — schema is volatile.*

### Audio Graph

```
Per instrument track:
  midiInput ──────┐
  midiSourceNode ─┤→ instrument → [fx…] ┬─ outputGain → masterGain
                                         ├─ sendGain1  → Bus1
                                         └─ sendGain2  → Bus2

Per audio input track:
  audioInput[ch] ─┐
  audioFileNode ──┤→ [fx…] → outputGain/sends → masterGain
  (mono inputs duplicated to stereo; input optional for playback-only tracks)

Per bus:    (summed sends) → [busFx…] → busOutputGain → masterGain
Master out: masterGain → [masterFx…] → audioOutput
```

`GraphWrapper` wraps the graph. In `processBlock` it: advances a sample-accurate beat clock; scans `Arrangement` for MIDI events in this buffer's beat range; routes to per-track `MidiSourceNode`s with sample offsets; captures live MIDI to `RecordFIFO` when recording; flushes notes (CC 123/120) on stop/seek/loop via per-channel bitset; then delegates to `graph.processBlock()`.

**MIDI gating:** disabled tracks (`audioEnabled=false` OR `midiEnabled=false`) receive no MIDI — both flags required in `rebuildConnections`. Actions like `setActiveTrack` set both together so the UI reflects action-driven changes.

**Audio device switching:** `AudioEngine` listens to `AudioDeviceManager`. On change, `rebuildGraph()` tears down IO nodes, reconfigures, recreates and rewires. Output and input devices are independent (CoreAudio); selection persists in `config["audio_output_device"]` / `["audio_input_device"]`.

### Subsystems (one-line index)

- **EngineSync** (`src/engine/EngineSync.*`) — pure event subscriber. Zero public methods.
- **AudioEngine** (`src/engine/AudioEngine.*`) — JUCE AudioProcessorGraph. UUID-keyed. Pure view of state.
- **GraphWrapper** (`src/engine/GraphWrapper.h`) — wraps graph for per-buffer MIDI scheduling and recording.
- **MidiSourceNode**, **AudioFileNode** — per-track sequencer playback nodes.
- **RecordFIFO**, **AudioRecordFIFO**, **AudioWriterThread** — lock-free SPSC capture + WAV writer with live peaks.
- **MIDIEngine** (`src/engine/MIDIEngine.*`) — MIDI input, learn mode (port-aware single-shot), routing, monitoring callback (single — last setter wins).
- **AutomationEngine** (`src/automation/`) — 60fps timer, interpolations with easing, delays.
- **InternalSequencer** (`src/daw/`) — own transport, tempo, beat clock. Thread-safe atomics.
- **Arrangement** (`src/daw/Arrangement.*`) — view over `TrackState.regions` for current song. Scan, record, region ops. Doesn't own data.
- **LuaEngine** (`src/scripting/`) — sol2-embedded Lua, takes State+Engine+Coordinator.
- **IPCServer** (`src/ipc/`) — Unix socket `/tmp/performance.sock`. `bin/perf` shell command.
- **SongRuntime** (`src/song/`) — MIDI control dispatch with wildcard fallback (exact → any device → any channel → any/any).

### GUI

All GUI components take `StateAPI&` + `EngineAPI&`. See `src/gui/` for individual files.

- **MainLayout** — toolbar + sidebar + dual-pane area + mixer.
- **ProducePane** — DAW arrange view: transport bar with LCD position, two-row track headers (power+name / M/S/R/I pills), timeline grid, regions (MIDI piano roll or audio waveform), action track. Region ops via Cmd/Shift selection + drag/keyboard. Auto-scroll, two-finger horizontal scroll. Keyboard: space/r/return, h/l (step by div), Shift+H/L (step by measure), Cmd+h/l/j/k (zoom).
- **MixerView** + **TrackStrip** / **BusStrip** / **OutputStrip** — 30Hz peak polling. Drag headers to reorder. Power icons toggle `audioEnabled`. M/S pills bottom row.
- **FaderMeter** — fader + L/R meters, IEC dB scale (-60 to +6), peak hold, click-to-jump, full range from handle center.
- **MusicalTyping** — Cmd+Shift+K. Logic-style keyboard layout, octave/velocity, sustain. Injects via `audioEngine.injectMidi()`.
- **MorphEditor** — slot-based compound morph editor.
- **PluginSlot**, **SendsPanel** — reusable controls.
- **Sidebar** — Songs/Library/Actions/Devices tabs. MIDI device entries informational only; audio devices clickable for I/O.
- **MappingPane** — Controllers (left) + Song Mappings (right, Atemporal + Score). Drag-and-drop between all areas. Learn mode is port-aware single-shot. Stub bindings (no action) persist.
- **DebugPane**, **LogPane**, **SettingsWindow** (Cmd+,), **ChatView**, **ClaudeClient**.
- **KeyBindingManager** — 36 commands across File/Edit/Transport/View/Region/Track. User overrides in config. `KeyBindingEditor` modal for rebinding. Pane toggles: ⌘Y Produce, ⌘U Mappings, ⌘I Chat, ⌘⇧L Logs, ⌘O Mixer, ⌘P Sidebar.

### Theme (`src/gui/Theme.h`)

All design tokens live here. This section is authoritative — read it before touching any GUI file.

#### Rules

1. **Never** use `juce::Colour(0x...)`, `juce::Colours::xxx`, or raw hex literals in GUI code. All colors come from `Theme::color(Theme::Color::xxx)`.
2. **Never** use raw font sizes (`juce::Font(14.0f)`, `Theme::font(22.0f)`). Use named tokens: `Theme::fontSizeLg`, `Theme::fontSizeSm`, etc.
3. **Never** use magic padding/spacing numbers where a token fits. Use `Theme::spacingXs/S/M/L/Xl`, `Theme::headerHeight`, `Theme::pillSize`, etc. (Layout math tied to local geometry — centering icons inside their own bounds, arc radii inside a knob — is fine; those aren't themeable.)
4. **`.withAlpha(x)` is allowed** on a theme token (e.g. `Theme::color(Theme::Color::accent).withAlpha(0.5f)`). Prefer this over a raw semi-transparent hex. If a semi-transparent color has a clear semantic role (drag-dim overlay, playhead line), add a dedicated token in Theme.h.
5. **Semantic naming over value reuse.** Several tokens share the same hex value (`bgSlot`, `bgControl`, `bgSelection` are all `0x2a2a2a`) — they're kept distinct so a future theme can vary them independently without grep-and-replace.
6. **When adding a new token**: add it to `Theme.h` in the appropriate category with a comment describing its use. Grep for similar call sites first — you may be duplicating an existing token.

#### Color tokens

**Surfaces (darkest → lightest):**

| Token | Value | Use |
|---|---|---|
| `bgApp` / `bgPanel` | `0xff161616` | App background, sidebar, pane headers |
| `bgSurface` | `0xff1e1e1e` | Track lanes, mixer strips, track headers |
| `bgSurfaceRaised` | `0xff333333` | Regions in the timeline — one step above the lane |
| `bgRecessed` | `0xff121212` | Deepest inset (candidate for removal; see Active Work) |

**Interactive controls** (pills, plugin slots, LCD, pickers, text fields):

| Token | Value | Use |
|---|---|---|
| `bgControl` | `0xff2d2d2d` | Resting state of any interactive control |
| `bgControlHover` | `0xff333333` | Hover state for any interactive control |
| `bgSelection` | `0xff262626` | Selected track row highlight — sits between `bgSurface` and `bgControl` so pills on selected rows still contrast |
| `bgOverlay` | `0xff2a2a2a` | Modal / popup backdrop |
| `bgDisabled` | `0xff252525` | Disabled strip / header fill |

**Passive inset surfaces** (not clickable):

| Token | Value | Use |
|---|---|---|
| `bgSlot` | `0xff2a2a2a` | Meter grooves, fader/slider troughs |
| `overlayDim` | `0x40000000` | Semi-transparent dim over content (drag source, etc.) |

**Text:**

| Token | Value | Use |
|---|---|---|
| `textPrimary` | `0xffd8d8d8` | Main body text, track names |
| `textSecondary` | `0xffaaaaaa` | Labels, descriptions |
| `textDim` | `0xff666666` | Disabled, placeholder, type indicators |
| `textOnColor` | `0xffffffff` | Text on colored backgrounds only (active pills, transport buttons) |
| `controlHandle` | `0xffffffff` | Fader handles, grabbable controls |

**Borders:**

| Token | Value | Use |
|---|---|---|
| `border` | `0xff444444` | Standard divider lines |
| `borderSubtle` | `0xff333333` | Lighter separators within panels |

**Accent:**

| Token | Value | Use |
|---|---|---|
| `accent` | `0xff2a6aaa` | Selection indicator, focus, drag-target line |
| `accentDim` | `0xff1a4a6a` | Subtle accent |

**Transport:**

| Token | Value | Use |
|---|---|---|
| `transportPlay` | `0xff2a6a2a` | Play button active bg |
| `transportRec` | `0xff6a2a2a` | Record mode active bg |
| `transportRecDot` | `0xffcc4444` | Record dot when idle |
| `transportCycle` | `0xff8a8a40` | Cycle active + cycle guide lines |
| `transportCycleOff` | `0xff505050` | Cycle button inactive |
| `playhead` | `0xccffffff` | Playhead vertical line (semi-transparent white) |

**Meter / activity:**

| Token | Value | Use |
|---|---|---|
| `meterGreen` | `0xff44cc44` | Safe level |
| `meterAmber` | `0xffccaa44` | Warning zone |
| `meterRed` | `0xffcc4444` | Clipping zone |
| `sendPeak` | `0xff1a6e1a` | Send knob peak-activity glow (darker green) |
| `activityOn` / `activityOff` | `0xff44cc44` / `0xff1a3a1a` | MIDI activity indicator |
| `statusError` | `0xff994444` | Load failures |

**Track pills** (M/S/R/I active colors):

| Token | Value | Use |
|---|---|---|
| `pillMute` | `0xff8a7a3a` | M active — muted gold |
| `pillSolo` | `0xff3a6a8a` | S active — muted steel |
| `pillArm` | `0xff8a4040` | R active — muted red |
| `pillInput` | `0xff8a4040` | I active — muted red |
| `pillTextOff` | `0xff888888` | Inactive pill text color |

Pill *resting* backgrounds use `bgControl`, *hover* uses `bgControlHover`. Pill text when active uses `textOnColor`. Pill hover only applies to resting (off) pills — active colored pills do not change on hover.

**Slot type tints, chat bubbles, track/device color palettes** — see `Theme.h` for the full list. These are category-specific and referenced only from their respective GUI files.

#### Typography

| Token | Value | Use |
|---|---|---|
| `fontSizeTitle` | `16.0f` | Pane headers, section titles |
| `fontSizeLg` | `14.0f` | Track names, primary content (alias: `fontSize`) |
| `fontSizeMd` | `13.0f` | Slot labels, secondary content |
| `fontSizeSm` | `12.0f` | Badges, hints, type indicators |
| `fontSizeXs` | `9.0f` | Very small labels (metronome, region counters) |
| `fontSizePill` | `13.0f` | Pill button labels |
| `fontSizeMeter` | `10.0f` | dB tick labels |
| `fontSizeLcdLg` / `LcdMd` / `LcdLabel` | `29 / 23 / 8` | Transport LCD digits + tiny labels |

Use `Theme::font(Theme::fontSizeXx)` for sans and `Theme::fontMono(Theme::fontSizeXx)` for monospace (LCD).

#### Spacing

| Token | Value |
|---|---|
| `spacingXs` | `2` |
| `spacingS` | `4` |
| `spacingM` | `8` |
| `spacingL` | `12` |
| `spacingXl` | `16` |

#### Component dimensions

- `headerHeight = 28` — shared height of mixer strip headers and ProducePane track name row. **Single source of truth for vertical name-block rhythm.**
- `slotHeight = 24`, `slotGap = 4`, `slotPadding = 4`
- `trackPadding = 12`, `trackStripWidth = 160`, `iconSize = 14`
- `pillSize = 20`, `pillRadius = 4.0f`, `pillGap = 5`, `pillGroupGap = 8`, `pillNameGap = 8`
- `cornerRadius = 6.0f`, `cornerRadiusSm = 4.0f`

#### Design principles

- **Minimal color, maximum contrast hierarchy.** The surface stack (`bgApp → bgSurface → bgControl/bgSlot/bgSelection → bgSurfaceRaised/bgControlHover`) carries most of the visual hierarchy. Color is reserved for meaning (transport, meter, status, pill states).
- **Neutral track headers.** No per-track color. Track type is indicated by affordances (audio inputs lack the instrument slot; action tracks hide the mixer), not color coding.
- **Two-row track headers in ProducePane:** name on top (`headerHeight = 28`), M/S/R/I pills below with a 10px gap, `trackRowHeight = 72` default.
- **Hover is an interaction signal, not decoration.** Only interactive controls get hover states, and only when resting (off/unselected). Active colored pills don't change on hover.
- **Semantic tokens over value reuse.** Duplicating a hex value across multiple semantic tokens is a feature — future themes can split them.

#### Adding a new theme

In principle: copy `Theme.h` to a new file, change color values, switch the included header. In practice, there's no runtime theme switching yet — and the non-pane files (several modals, dialogs, overlays) still contain hardcoded colors. Full themeability requires finishing the pane sweep in Active Work.

### Bindings & Actions

Bindings map MIDI controls to named actions with arguments. Two scopes (song / global) merged via `effectiveBindings()` (song wins). Args stored as JSON arrays with track UUIDs. `paramSchema` on actions drives MappingPane input fields. `ActionInfo.durationParamIndex` identifies the duration arg for UI duration bars. `resolveTrack()` expects UUIDs only.

Built-in actions: `setActiveTrack`, `enableTrack`, `disableTrack`, `fadeOut`, `fadeIn`, `crossfade`, `trackVolume`, `morphToPreset`, `morphChain`, `morph`. SongRuntime dispatches with wildcard fallback (exact → any device → any channel → any/any).

### Plugin State Presets

Stored per plugin in `~/.config/performance/snapshots/<pluginName>/<presetName>.state`. Binary blobs via JUCE `getStateInformation`/`setStateInformation`. Referenced by UUID. `PresetKind`: Instrument, Effect, Track. Track presets (`~/.config/performance/track_presets/<name>.json`) capture the full chain.

### Third-party AU Plugin Loading

Index `.component` Info.plist metadata at startup, on-demand `AudioComponentRegister`. Cache: `~/.config/performance/plugin-cache.xml`.

### Logging

`perfLog()` → stderr + `/tmp/performance.log` (unbuffered, ISO 8601 UTC). Subsystem prefixes: `[Engine]`, `[EngineSync]`, `[Coordinator]`, `[MIDI]`, `[Persistence]`, `[Sidebar]`, `[IPC]`. Tail with `tail -f /tmp/performance.log`. In-app LogPane provides selectable view.

### IPC

`bin/perf` shell command sends Lua to the running app via `/tmp/performance.sock`. `runtime/CLAUDE.md` is the prompt for the embedded Claude instance.

## Tests

Single file: `tests/PerformanceTests.cpp`, JUCE `UnitTest` framework, ~134 tests, all isolated. `MockAudioEngine` for EngineSync verification. Coverage gaps and per-class breakdown in `DEV_HISTORY.md`.
