# Live Performance Environment

A scriptable runtime for live music performance on macOS. Solo performer, centered around an Arturia KeyLab 88 MkII and Audio Unit plugins. The app is a live environment — always running, always ready. An in-memory state store is the single source of truth at runtime. SQLite is the persistence layer (load on startup, save on demand). The audio engine is a pure view of state.

**Load-bearing side docs — read before touching the relevant area:**

- `DEV_HISTORY.md` — changelog, completed work, test inventory. `git log` is still authoritative for *what* changed; this is context that doesn't fit in commit messages.
- `docs/INCIDENT_2026-04-18_PERSISTENCE.md` — first-session data-loss retro + architectural hardening plan. Load-bearing when scoping 0.2.x work.
- `docs/ACTION_ALGEBRA.md` — compositional action core. Read before adding/modifying built-in actions or touching bounce.
- `docs/ACTION_INSTANCES_REFACTOR.md` — typed ParamSchema + action-instance form. Read before touching action registration or the param schema.
- `docs/PRODUCE_PANE_REFACTOR.md` — audibility model + visual layer. Read before adding visual axes to track-row or region rendering.
- `docs/LAMBDA_CHAT_PROXY.md` — AI-for-testers Lambda design + implementation.
- `docs/BUNDLED_PLUGINS.md` — first-launch plugin pack. The "ruled out" list is load-bearing — free-plugin licenses are narrow and easy to violate.
- `docs/COMPOSER_INTEGRATION.md` — composer pipeline (notation → StateAPI regions). Read before touching the v2 parser, writer, or compose prompt.
- `docs/DAW_BRIDGE_PLAN.md` — forward-looking DAW bridge design.
- `docs/LIVE_LOOPING.md` — live-looping design + phased plan. **In progress:** Phases 1–4 landed, initial click-testing underway, Phase 5 not started. Read before touching looper pane, `track.loops`, `pendingTakeId`, or cycle-wrap logic.

User-testing artifacts (round plans, session notes, tester profiles, feedback) live in the separate private repo `willhaslett/performance-testing`.

## Version & Distribution

**Current version: `0.0.3`** — SSOT is `CMakeLists.txt` line 2: `project(Performance VERSION 0.0.3)`. `0.1.0` will be the first beta. `0.0.3` adds the bundled-plugin install pack (15 curated free AU plugins via private S3 + presigned Lambda, first-launch prompt, Settings → Plugins install/uninstall) on top of `0.0.2`'s architectural settlement.

**Build pipeline:** `scripts/build-release.sh [version]` — one command for Release build → code sign → DMG → notarize → staple. Version defaults to the CMake version. Output: `dist/Performance-<version>.dmg`. Requires Apple Developer ID certificate (William Haslett, H25TK2U8FA) + keychain-stored notarization credentials (`AC_PASSWORD` profile). First successful end-to-end run completed.

**Beta expiry:** compiled-in date check in `main.mm` — currently October 16, 2026 (6 months from April 2026). Shows dialog and quits if expired. Update the `juce::Time` constructor for each release cycle.

**Binary:** Universal (arm64 + x86_64), deployment target macOS 11.0 (Big Sur).

**Toolbar:** minimal — just build info (commit hash, selectable/copyable TextEditor, `textDim`/`fontSizeSm`) right-aligned. When the commit is tagged, the tag appears alongside. Commit hash is regenerated on every build via `cmake/GenerateBuildVersion.cmake` → `build/generated/BuildVersion.h`, and auto-appends `-dirty` when there are uncommitted changes. No reconfigure needed to refresh it. Every log file also records `[App] Build version=... commit=... tag=... dirty=...` on startup.

**Reset script:** `bin/reset` — scorched-earth reset of `~/.config/performance/` preserving `keys/` (notarization + telemetry config) and `plugin-cache.xml` (AU scan cache). Does not touch `~/Library/Application Support/com.performance.app/install.json`, so a reset keeps the same installation identity. Simulates "reset song library," not "fresh install."

## 0.1.0 Release Plan

Target: first beta, roughly a week out. Not a rush. Ship gate: §1–3 done, §4 decided (in or explicitly deferred), §3 in or explicitly backed out. Then bump `CMakeLists.txt` to `0.1.0`, tag, run `scripts/build-release.sh`, upload to Drive, distribute to 4 musician friends. This is the last round before strangers.

### Current focus / recommended sequence

As of 2026-04-20, the **architectural foundation is settled.** Four deep passes landed on the 0.0.2 checkpoint: typed IDs (all entity IDs are newtypes; whole persistence bug class is now a compile error), the produce-pane refactor (derived visual model), the action-instances refactor (typed ParamSchema + unified form + cascade-delete), and the action-algebra refactor (six-primitive core replacing the hardcoded dispatch ladder). Each has its own design doc — see the side-docs list above. AI-for-testers chat (proxy Lambda, SSE streaming, monthly token caps) and bounce-to-WAV both landed alongside.

**0.1.0 plan — reassessed.** Architecture is settled; the remaining gates are about whether the app *rewards* a tester's hour. The two major features before ship both landed on 2026-04-20: bundled-plugin install pack (15 curated free AU plugins via S3 + presigned Lambda; see `docs/BUNDLED_PLUGINS.md`) and composer integration (chat-driven MIDI composition writing directly onto project tracks; see `docs/COMPOSER_INTEGRATION.md`).

Remaining sequence:

1. ~~**Bundled plugin install pack.**~~ *Shipped (2026-04-20).* 15 curated free AU plugins (6 instruments, 9 effects — mda suite from `hollance/mda-plugins-juce` built from source, plus Dexed, Surge XT + Effects, Airwindows Consolidated downloaded + re-signed). The DMG stays small because plugins aren't in-bundle: they live in a private S3 bucket fronted by a `PluginsProxy` Lambda that hands out 1-hour presigned URLs to any app presenting the shared bearer. First-launch prompt kicks off download → SHA-256 verify → extract to `~/Library/Audio/Plug-Ins/Components/` → rescan engine → sync catalog. Help → Install Plugin Pack and Settings → Plugins (per-archive Remove + Remove all) provide the ongoing surfaces. See `docs/BUNDLED_PLUGINS.md` for the pipeline; `scripts/bundled-plugins/` for `build-mda.sh` / `download-*.sh` / `sign-all.sh` / `notarize-all.sh` / `package-all.sh` / `publish.sh`; `infra/lib/plugins-stack.ts` + `infra/lambda/plugins.ts` for the server side.
2. ~~**Composer integration.**~~ *Shipped (2026-04-20).* Chat pane gained a latching Compose toggle that swaps in a composer system prompt (adapted from `~/ideas_and_projects/dawai/`) and exposes a `compose(notation, startBeat)` Lua tool. The LLM emits dawai's v2 text notation; our C++ port of its compiler (`src/composer/`) parses it into a canonical `ComposerOutput` struct and a pluggable `NotationParser` interface; a `ComposerWriter` creates regions directly on the project's tracks via the normal StateAPI path. No MIDI file intermediate, no Python runtime, no Lambda changes — the app sends the composer prompt to the existing chat proxy. Notes play through whatever AU plugin the track has loaded. Undo is the "reject" affordance. Prompt-tuning for placement (region start / bar ranges) and quality is ongoing per `docs/COMPOSER_INTEGRATION.md`.
3. **perfuce.com rebuild** (several days). Tester onboarding copy, example prompts, "chat is free for testers" line. Mostly gated on video capture.
4. **Distribution proof** (second-machine install, ~1 hour).
5. **Tag + release.**

### 1. Distribution proof

Pipeline is built but has only ever run on the dev machine. Before shipping:

- [ ] Upload `dist/Performance-0.1.0.dmg` to Drive
- [ ] Install on a second Mac (ideally a fresh user account)
- [ ] Gatekeeper accepts the notarized DMG without warnings
- [ ] First launch creates a new install ID in `install.json`
- [ ] Default song reaches sound-on-first-keypress
- [ ] Session log lands in the S3 bucket + DDB row appears
- [ ] Force a crash (`kill -9` mid-session); `.prev` log ships on next launch

### 2. First-friend checklist

Items still open:

- [ ] **perfuce.com rebuilt as the single docs/reference surface.** Three featured sections (AI / sequencer / perform), each with a looping UI demo video. New `/docs` route with step-by-step guides per pane. See `../performance-testing/rounds/01-v0.1.0-first-friends/02-materials/perfuce-site-plan.md`.

Completed pre-beta items archived in `DEV_HISTORY.md` (code signing, beta expiry, universal binary, startup chooser, autosave debounce, persistence integrity fix, audio device auto-select, Export Logs, feedback channel, etc).

### 3. Built-in AI for testers

Shipped. Chat pane with Lambda-proxied Claude (no key on tester machines), SSE streaming, per-install monthly token caps, safe-default gain rules, hidden tool-call surface, Secrets Manager bearer. Full design + shipped-detail in `docs/LAMBDA_CHAT_PROXY.md`.

Open tails:

- [ ] **Tool-use surface audit** — safe-default rules cover the biggest risks; destructive-op gating (confirm / dry-run) still open.
- [ ] **ChatView UX polish** — error states, history persistence, clear chat, cancel in-flight, auto-focus input on pane reveal.
- [ ] **Self-test round** — ongoing through local use.
- [ ] **Tester onboarding copy** — 3–4 example prompts on perfuce.com. Carry-over to §1 above.

### 4. Nice-to-haves considered

- [x] **Bounce to stereo file** — shipped. File → Bounce…; cycle-aware; faster-than-realtime via `OfflineRenderer`. Lua: `bounce(path[, startBeat, endBeat])`. Punts documented in `DEV_HISTORY.md` (constant tempo/time-sig, frozen automation, master-only, hard cutoff at end beat). Production-grade would require TempoMap runtime + tail-time option + stem bouncing.
- [ ] (open — fill in as testing surfaces asks)

### 5. Explicitly deferred to 0.2.x

Named so it's a decision, not a gap:

- Failed plugin load UI feedback (no issues in months; telemetry will surface; revisit if a tester hits it)
- Bring-your-own-key Settings field (not needed for the friends round; Lambda proxy covers everyone)
- Theme picker UI (backend ready, UI deferred)
- Remaining theming sweep (DebugPane, LogPane, ChatView, SettingsWindow, MusicalTyping, MorphEditor, KeyBindingEditor, SaveAsDialog, MainLayout overlay)
- MIDI effects (transpose, channel filter, arpeggiator)
- LCD interactivity (drag / double-click to edit)
- TempoMap + TimeSignatureMap runtime evaluation
- Background plugin state capture
- Settings MIDI tab content
- ⌘O Songs palette
- VST3 hosting (unblocks Dragonfly + many other free plugins where AU was dropped or never shipped; scope ~1–2 days for `JUCE_PLUGINHOST_VST3=1`, VST3 scanner wiring, and generalizing plugin identifiers across the registry). See `docs/BUNDLED_PLUGINS.md`.

## Active Work

### First-user-testing push

The immediate goal is getting the app in the hands of 4 musician friends. Focus is on UX that makes the app self-explanatory for a non-developer musician who records real instruments.

### Startup flow

**Zero songs (first-ever launch):** auto-creates "Untitled" with DLS Electric Piano (Track 1, MIDI program 4) + Audio In (Track 2, disarmed, input monitoring off). User presses a key, hears sound.

**1+ songs (subsequent launches):** themed startup chooser card — "Choose a Song" title, inset list pane with song rows (`bgControl`/`bgStripe` alternating, accent-blue hover), "Create New Song" button (accent outline, accent fill on hover). No way to dismiss without choosing — must-act modal.

**Deleting all songs:** auto-creates a new "Untitled" default song. Never leaves zero songs.

**Plugin scan:** deferred to after the window is visible. Shows "Scanning plugins..." overlay. 100ms timer delay ensures the overlay paints before the blocking scan starts. Only triggers when `plugin-cache.xml` is missing.

### Save model

**Fully automatic.** No manual save needed. Debounced: StateEvent subscription stamps `lastStateChangeMs` on every mutation; 60Hz timer flushes to SQLite after 3 seconds of quiet. Explicit `save()` still exists for quit, song-switch, and File → Save (force flush). The concept of "unsaved changes" does not exist — undo is the safety net.

### Sidebar

Flat categorized list (no tabs, no tree). Two sections with title-case headers at `fontSizeLg`:

**View** — one row per pane with a monospaced keybinding hint right of a fixed-width label column. Rows: Produce ⌘Y · Perform ⌘U · Chat ⌘I · Mixer ⌘O. Sidebar itself is not a row — toggle via ⌘P or the View menu. Active panes are indicated by text color only (no accent bar, no row highlight — pane visibility is self-evident).

**Songs** — list of all songs (click to load) + "+ New Song" action button. Current song stays highlighted with `bgListActive` (brighter than `bgSelection` — a separate token because the list-selection context doesn't have embedded pills to contrast against).

Themed: `bgSurface` container, `fontSizeLg` headers + items, `fontMono(fontSizeKeyHint)` key hints in `textKeyHint`, `bgControlHover` on hover.

### Navigation model

Every pane is a simple show/hide toggle — no modes, no workspace concept. A mode-switcher experiment (toolbar pill with per-mode layout state) was tried and reverted — the complexity wasn't justified for what's really just one pane swapping.

**Pane slots:** Sidebar (left column), Left (main area), Right (secondary), Bottom (mixer). Each slot holds one content type. No slot-pairing games — every content type fits in exactly one slot.

**PerformPane** composes `ControllersPane` + `SongMappingsPane` side-by-side with an internal draggable divider, and lives in the Left slot as a single `PaneContent::Perform`. This avoided an orphaning bug where the old dual-slot Controllers/SongMappings arrangement left one half visible if anything else took the Right slot. The two inner panes remain independent classes; `PerformPane` is a thin composer, so pulling them apart later stays cheap.

**Per-content preferred widths (for Left/Right split):** `Produce = 0.65`, `Perform = 0.75`, `Debug = 0.50`.

### Theme system

Runtime-mutable. Every token in `Theme.h` (colors, fonts, spacing, dimensions, radii — 80+ values) is `inline` non-const and loadable from JSON at startup. Call sites unchanged.

**Multi-source:** factory themes (compiled into binary via `juce_add_binary_data` from `runtime/themes/*.json`) + user themes (`~/.config/performance/themes/*.json`, override factory on id collision). `Theme::loadThemeById(id)` resolves across both. `config["active_theme"]` defaults to `"minimal_dark"`. Two factory themes: `minimal_dark`, `minimal_light`.

**LookAndFeel:** `PerformanceLookAndFeel` set as app-wide default at startup. Styles popup menus, combo boxes, scroll bars, document window chrome.

**Rules:** no raw hex colors, no raw font sizes, no magic spacing — everything through `Theme.h` tokens.

### Theming sweep status

**Done:** ProducePane, MixerView family, ControllersPane, SongMappingsPane, PerformPane, Sidebar.
**Not swept:** DebugPane, LogPane, ChatView, SettingsWindow, MusicalTyping, MorphEditor, KeyBindingEditor, SaveAsDialog, MainLayout overlay.

### Design document

`docs/GUI_DESIGN.md` — clean-sheet UX thinking. User journeys (first sound, building, performance setup, performing, refinement), UX principles (two modes/one app, Claude as universal shortcut, progressive disclosure), the performance dashboard question, navigation model options. WIP conversation artifact, not a spec.

### Smaller design questions still open

- Fader handle shape
- Selected track vs region contrast (`bgSelection` vs `bgSurfaceRaised`)
- Remove `bgRecessed` if unused
- SongMappingsPane hover affordances (`[+]` buttons, group field, disclosure triangles)
- Liveliness of sparse panes

## Backlog

**High priority:**
- **First-run audio device auto-selection** — auto-select system default output on first launch.
- **Failed plugin load feedback** — show plugin name in error color when load fails.
- **LCD interactivity** — drag-to-change and double-click-to-edit for BAR/BEAT/DIV/TICK + time display.
- **Stuck note prevention at region boundaries** — synthetic noteOffs at region end.
- **TempoMap + TimeSignatureMap** — runtime evaluation of tempo/time-sig change events.
- **Theme picker UI** — menu or settings entry to switch themes. `availableThemes()` is ready.
- **Auto-focus chat input when Chat pane is revealed** — currently testers have to click the field before typing.

**Longer-term:**
- Refactor oversized GUI files (ProducePane ~2520 lines).
- MIDI effects (transpose, channel filter, arpeggiator).
- Fader/knob drag stops at screen edge.
- Background plugin state capture (getStateInformation off message thread).
- Settings window MIDI tab content.
- ⌘O Songs palette (type-to-filter overlay for performance-time song switching).

**Unverified behaviors — revisit if a tester hits one or when next touching the area:**

Pulled from the pre-ship punch list after Will reported extensive click-testing shook none of these out. Not 0.1.0 blockers; test deliberately before relying on any of them in a harder-edge scenario.

- State-management mutation paths — audit GUI / Lua / IPC / MIDI-binding / EngineSync paths for StateAPI-only discipline; registry↔engine consistency; re-enable track preset load.
- Recording round-trips — audio + MIDI. Arm, record, stop, replay, quit, relaunch, replay again. Both types, with and without plugins in the chain.
- Plugin state save/load on relaunch — third-party AU plugins restore patch + parameters correctly after an app restart.
- Song-switching in performance — rapid switching under MIDI activity: no stuck notes, no lingering audio, UI reflects the new song immediately.
- Autosave under stress — ~50 mutations in 10 seconds: debounce holds, no partial writes, backup file consistent.

## Core Concepts

- **The app is an environment** — launches and restores state from SQLite. **Fully autosaved** — debounced 3 seconds after last change. No manual save needed; undo is the safety net. File → Save forces an immediate flush but is never required.
- **Song** — a named session with its own tracks, busses, sends, bindings. Switching songs clears the engine and rebuilds from state. No "Sandbox" — every song is a regular song. First launch creates "Untitled" with a default template (DLS Electric Piano + Audio In). Deleting all songs auto-creates a new default.
- **Bindings** — MIDI controls bind to named actions with entity ID arguments. Two scopes: global (always active) and song-scoped. *In practice, all current bindings are song-scoped.*
- **Score** — an ordered list of action references per song. Used for development ("go to step N" replays from initial state) and as documentation of performance transitions.
- **Automation** — `interpolate(from, to, duration, callback, easing)` and `delay(seconds, callback)` with actions: `fadeOut`, `fadeIn`, `crossfade`, `morphToPreset`, `morphChain`, `morph` (compound bundling parallel/sequential sub-actions).
- **Authoring model** — Claude runs embedded in the app (native chat UI, Claude API with tool use). Will plays and directs, Claude modifies the environment via the `perf` tool (Lua execution). The GUI provides direct manipulation. All consumers use the same APIs.

## Architecture

Detailed diagrams (data flow, state-model tree, audio graph) live in
`docs/ARCHITECTURE.md`. This section captures the rules and constraints
that shape new code.

### Data flow (summary)

Mutations (GUI, Lua, IPC, MIDI bindings) → **StateAPI** → emits event → **EngineSync** applies it to the **AudioEngine**. **PersistenceLayer** loads/saves `AppState` atomically via `replaceState()`. The audio engine is a pure view of state; nothing writes to it except EngineSync.

### Three APIs

- **StateAPI** (`src/api/StateAPI.h/.cpp`) — all state reads/writes. In-memory C++ structs (`src/state/StateModel.h`), observable via `StateEventBus`. No JUCE dependency, no SQLite. Default consumer.
- **EngineAPI** (`src/api/EngineAPI.h/.cpp`) — engine-only concerns: peak levels, processor access (for presets/params), plugin editor windows, plugin discovery. Use only when state can't provide it.
- **PerformanceCoordinator** (`src/api/PerformanceCoordinator.h/.cpp`) — lifecycle and orchestration: init/shutdown, song management, track presets, automation, action dispatch. Owns all subsystems, exposes `state()` and `engine()`.

### Rules for new code

- All state reads/writes go through StateAPI. Never call AudioEngine for state.
- EngineAPI is for peak levels, processors, plugin UI, and plugin discovery only.
- EngineSync is the only code that calls AudioEngine write methods.
- NEVER use names as keys or identity. UUIDs everywhere — every track, bus, effect, send gets one at creation; names are display-only. Names resolve to UUIDs once at Lua/Claude entry.
- When adding a new settable value: add to StateModel, add StateAPI method, handle in EngineSync.
- Fail loud: use `PERF_ASSERT` and the asserting helpers `song()`/`track()`/`bus()`/`device()` in StateAPI. Don't paper over programming errors.

### Key state-model facts

- **Track source types:** `Instrument` (MIDI→plugin→audio), `AudioInput` (physical input→fx→output, mono→stereo upmix), `Action` (beat-triggered, no audio, hidden from mixer).
- **No track-level on/off.** Logic-style: `muted` is the only track-level silencer. There is no `audioEnabled` / `midiEnabled` / `masterAudioEnabled`. `setActiveTrack` just selects.
- `armed`, `muted`, `soloed`, `recordModeActive` — runtime, not persisted. Recording is explicit: armed tracks record only when record mode is active.
- Regions are take folders. `MidiEventState` is raw events; notes derived via `buildNoteList()`.
- Action track: one per song (auto-created), no regions — events stored directly with absolute beat positions.

### Persistence

`src/persistence/PersistenceLayer.h/.cpp`. SQLite normalized schema at `~/.config/performance/state.db` (with `state.bak.db` backup on each save). `loadInto()` builds a plain `AppState` then calls `state.replaceState()` (atomic swap, one event). Schema version tracked. **No migration shims at this stage — schema is volatile.**

### Audio engine notes

Graph diagram + `GraphWrapper::processBlock` details in `docs/ARCHITECTURE.md`. Key constraints:

- **No graph-level gating.** Every track and bus is wired into the graph. Mute lives at the per-track gain stage; nothing else gates routing.
- **Audio device switching:** `AudioEngine` listens to `AudioDeviceManager`. On change, `rebuildGraph()` tears down IO nodes, reconfigures, recreates, rewires. Output and input devices are independent (CoreAudio); selection persists in `config["audio_output_device"]` / `["audio_input_device"]`.

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
- **ProducePane** — DAW arrange view: transport bar with LCD position, two-row track headers (name / M/S/R/I pills), timeline grid, regions (MIDI piano roll or audio waveform), action track. Paint flows through a derived visual model (`Audibility` enum + `TrackRowVisuals` / `RegionVisuals` structs + `paintTrackRow` / `paintRegionShell` helpers) — add new visual axes there, not in scattered conditionals. Region ops via Cmd/Shift selection + drag/keyboard. Auto-scroll, two-finger horizontal scroll. Keyboard: space/r/return, h/l (step by div), Shift+H/L (step by measure), Cmd+h/l/j/k (zoom).
- **MixerView** + **TrackStrip** / **BusStrip** / **OutputStrip** — 30Hz peak polling. Drag headers to reorder. M/S pills bottom row.
- **FaderMeter** — fader + L/R meters, IEC dB scale (-60 to +6), peak hold, click-to-jump, full range from handle center.
- **MusicalTyping** — Cmd+Shift+K. Logic-style keyboard layout, octave/velocity, sustain. Injects via `audioEngine.injectMidi()`.
- **MorphEditor** — slot-based compound morph editor.
- **PluginSlot**, **SendsPanel** — reusable controls.
- **Sidebar** — flat View + Songs list (see Active Work § Sidebar for details).
- **PerformPane** — thin composer wrapping **ControllersPane** (MIDI device tree, learn mode) + **SongMappingsPane** (Atemporal + Score bindings) with an internal draggable divider. Drag-and-drop between all areas. Learn mode is port-aware single-shot. Stub bindings (no action) persist.
- **DebugPane**, **LogPane**, **SettingsWindow** (Cmd+, — Audio / MIDI / About tabs, About shows install ID + diagnostics toggle), **ChatView**, **ClaudeClient**.
- **KeyBindingManager** — 36 commands across File/Edit/Transport/View/Region/Track. User overrides in config. `KeyBindingEditor` modal for rebinding. Pane toggles: ⌘Y Produce, ⌘U Mappings, ⌘I Chat, ⌘⇧L Logs, ⌘O Mixer, ⌘P Sidebar.

### Theme

Tokens live in `src/gui/Theme.h` (authoritative values). **Full reference — all color tables, typography, spacing, token categories, design principles — lives in `docs/THEME.md`. Read that before touching tokens or adding new ones.**

Six non-negotiable rules for GUI code:

1. **No raw hex colors, `juce::Colour(0x...)`, or `juce::Colours::xxx`** — use `Theme::color(Theme::Color::xxx)`.
2. **No raw font sizes** — use named tokens (`Theme::fontSizeLg`, `fontSizeSm`, etc.).
3. **No magic padding/spacing numbers where a token fits** — use `Theme::spacingXs/S/M/L/Xl`, `headerHeight`, `pillSize`, etc. Layout math tied to local geometry (centering icons in their own bounds, arc radii) is fine; it isn't themeable.
4. **`.withAlpha(x)` on a theme token is allowed.** If a semi-transparent color has a clear semantic role, add a dedicated token in `Theme.h` instead.
5. **Semantic naming over value reuse.** Some tokens share a hex value (`bgSlot`, `bgControl`, `bgSelection` are all `0x2a2a2a`) — kept distinct so a future theme can vary them independently.
6. **When adding a new token**: add it to `Theme.h` with a comment, grep for similar call sites first, update `docs/THEME.md` and the factory JSON themes in `runtime/themes/`.

### Bindings & Actions

Bindings map MIDI controls to named actions with arguments. Two scopes (song / global) merged via `effectiveBindings()` (song wins). Args stored as JSON arrays with track UUIDs. `paramSchema` on actions drives MappingPane input fields. `ActionInfo.durationParamIndex` identifies the duration arg for UI duration bars. `resolveTrack()` expects UUIDs only.

Built-in actions: `setActiveTrack`, `fadeOut`, `fadeIn`, `crossfade`, `trackVolume`, `morphToPreset`, `morphChain`, `morph`. SongRuntime dispatches with wildcard fallback (exact → any device → any channel → any/any).

### Plugin State Presets

Stored per plugin in `~/.config/performance/snapshots/<pluginName>/<presetName>.state`. Binary blobs via JUCE `getStateInformation`/`setStateInformation`. Referenced by UUID. `PresetKind`: Instrument, Effect, Track. Track presets (`~/.config/performance/track_presets/<name>.json`) capture the full chain.

### Third-party AU Plugin Loading

Index `.component` Info.plist metadata at startup, on-demand `AudioComponentRegister`. Cache: `~/.config/performance/plugin-cache.xml`.

### Logging

`perfLog()` → stderr + `/tmp/performance.log` (unbuffered, ISO 8601 UTC). Subsystem prefixes: `[Engine]`, `[EngineSync]`, `[Coordinator]`, `[MIDI]`, `[Persistence]`, `[Sidebar]`, `[IPC]`, `[Telemetry]`. Tail with `tail -f /tmp/performance.log`. In-app LogPane provides selectable view.

On startup, `initLog()` rescues any non-empty prior-session log to `/tmp/performance.log.<epoch>.prev` (so crashes/force-quits are retained), then opens a fresh log. The startup block emits `[App] Build version=... commit=... tag=... dirty=...` and `[App] Install id=... firstSeen=...` so every log file is self-identifying.

### Install identity & telemetry

- **`InstallId`** (`src/telemetry/`) — UUID at `~/Library/Application Support/com.performance.app/install.json`. Survives state resets + version upgrades. Shown in Settings > About.
- **`TelemetryShipper`** — startup-time fire-and-forget POST of any `/tmp/performance.log.*.prev` file to the ingest endpoint. Respects the "Send Diagnostics" Settings toggle. Deletes on success, retains on failure (auto-retry next launch).
- **Build-time config** — `cmake/GenerateBuildConfig.cmake` writes `build/generated/BuildConfig.h` with the Lambda URLs + bearer token from `keys/telemetry.json` (gitignored; populate via `scripts/fetch-telemetry-config.sh`). Absent file = empty values = shipper/chat/plugins-proxy all no-op, so fresh checkouts still build.
- **AWS infra** — two CDK stacks (`PerformanceTelemetry` + `PerformancePlugins`). Full design, deploy commands, and rotation steps in `infra/README.md`.

### IPC

`bin/perf` shell command sends Lua to the running app via `/tmp/performance.sock`. `runtime/CLAUDE.md` is the prompt for the embedded Claude instance.

## Tests

Single file: `tests/PerformanceTests.cpp`, JUCE `UnitTest` framework, 165 tests, all isolated. `MockAudioEngine` for EngineSync verification. BundledPluginInstaller tests use a file-path test hook (`setInstallManifestFileForTests`) to redirect the install-manifest path to a temp dir. Coverage gaps and per-class breakdown in `DEV_HISTORY.md`.
