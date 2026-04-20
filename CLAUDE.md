# Live Performance Environment

A scriptable runtime for live music performance on macOS. Solo performer, centered around an Arturia KeyLab 88 MkII and Audio Unit plugins. The app is a live environment — always running, always ready. An in-memory state store is the single source of truth at runtime. SQLite is the persistence layer (load on startup, save on demand). The audio engine is a pure view of state.

> Changelog, completed work, test inventory, and known issues live in `DEV_HISTORY.md`. Forward-looking DAW bridge design lives in `docs/DAW_BRIDGE_PLAN.md`. **`docs/LAMBDA_CHAT_PROXY.md`** is the design + implementation plan for the AI-for-testers Lambda — read before starting that work. **`docs/PRODUCE_PANE_REFACTOR.md`** is the design + step-plan retro for the audibility-model + visual-layer rework (now shipped) — read before adding new visual axes to track-row or region rendering. **`docs/ACTION_INSTANCES_REFACTOR.md`** is the design + step plan for typed-schema action instantiation, one unified form, and cascade-delete — read before touching action registration, action-instance creation UI, or the param schema format. **`docs/ACTION_ALGEBRA.md`** is the design + step plan for the compositional action core that replaces the hardcoded dispatch ladder with a six-primitive algebra + tree interpreter — read before adding or modifying built-in actions, the action-execution path, or bounce/offline-render integration. **`docs/INCIDENT_2026-04-18_PERSISTENCE.md` is the incident retro for the first-session data-loss bug and the prioritized architectural hardening plan — treat it as load-bearing when scoping 0.1.0 / 0.2.x work.** User-testing artifacts (round plans, session notes, tester profiles, feedback) live in the separate private repo `willhaslett/performance-testing`. Authoritative history is `git log`.

## Version & Distribution

**Current version: `0.0.1`** — SSOT is `CMakeLists.txt` line 2: `project(Performance VERSION 0.0.1)`. `0.1.0` will be the first beta.

**Build pipeline:** `scripts/build-release.sh [version]` — one command for Release build → code sign → DMG → notarize → staple. Version defaults to the CMake version. Output: `dist/Performance-<version>.dmg`. Requires Apple Developer ID certificate (William Haslett, H25TK2U8FA) + keychain-stored notarization credentials (`AC_PASSWORD` profile). First successful end-to-end run completed.

**Beta expiry:** compiled-in date check in `main.mm` — currently October 16, 2026 (6 months from April 2026). Shows dialog and quits if expired. Update the `juce::Time` constructor for each release cycle.

**Binary:** Universal (arm64 + x86_64), deployment target macOS 11.0 (Big Sur).

**Toolbar:** minimal — just build info (commit hash, selectable/copyable TextEditor, `textDim`/`fontSizeSm`) right-aligned. When the commit is tagged, the tag appears alongside. Commit hash is regenerated on every build via `cmake/GenerateBuildVersion.cmake` → `build/generated/BuildVersion.h`, and auto-appends `-dirty` when there are uncommitted changes. No reconfigure needed to refresh it. Every log file also records `[App] Build version=... commit=... tag=... dirty=...` on startup.

**Reset script:** `bin/reset` — scorched-earth reset of `~/.config/performance/` preserving `keys/` (notarization + telemetry config) and `plugin-cache.xml` (AU scan cache). Does not touch `~/Library/Application Support/com.performance.app/install.json`, so a reset keeps the same installation identity. Simulates "reset song library," not "fresh install."

## 0.1.0 Release Plan

Target: first beta, roughly a week out. Not a rush. Ship gate: §1–3 done, §4 decided (in or explicitly deferred), §3 in or explicitly backed out. Then bump `CMakeLists.txt` to `0.1.0`, tag, run `scripts/build-release.sh`, upload to Drive, distribute to 4 musician friends. This is the last round before strangers.

### Current focus / recommended sequence

As of 2026-04-20, the persistence data-loss incident is resolved (see `docs/INCIDENT_2026-04-18_PERSISTENCE.md`), bounce shipped end-to-end, **typed IDs are complete** (all 13 entity ID families are strongly-typed newtypes; the incident's root-cause bug class is now a compile error), the **AI-for-testers chunk is done** end-to-end (chat proxy Lambda deployed, C++ client wired with SSE streaming, per-install monthly token caps live, bearer token migrated to Secrets Manager, Show Log + Export Logs UI shipped), the **produce-pane refactor is done** — Phase 1 dropped redundant `audioEnabled` / `midiEnabled` / `masterAudioEnabled` state and the U power icon (Logic-style: mute is the only track-level silencer), Phase 2 replaced ad-hoc paint logic with a derived visual model (`Audibility` enum, `TrackRowVisuals` / `RegionVisuals` structs, two paint helpers), and the **action-instances refactor is done** on branch `action-instances-refactor` (ready to merge) — typed `ParamSchema` with five primitives (`channelRef`, `presetRef`, `enum`, `float`, `morph`) is the sole source of truth (legacy JSON string dropped, persistence round-trips typed format); `ActionInstanceForm` (schema-driven typed-widget form with validation) replaces the three ad-hoc creation dialogs; `ActionPicker` (two-state borderless popup: hover-highlighted action list → embedded form, escape navigates back) is used by all four creation paths (ProducePane action-track create + replace, MorphEditor sub-action, SongMappingsPane binding); env-availability filter (`actionCanInstantiate`) disables unsatisfiable actions; `humanizeLabel` turns camelCase into Title Case; `SongMappingsPane::formatArgs` resolves ref UUIDs to display names via the typed schema; **cascade-delete with confirmation** on track / bus removal walks dependent action events + bindings and prompts the user before removing together (`state/ActionRefs.*`); **load-time repair dialog** scans for stale refs after every song-load and lets the user clean them up; `runtime/CLAUDE.md` documents the schema grammar so the embedded Claude generates schemas the form can render. Full design in `docs/ACTION_INSTANCES_REFACTOR.md`. What's left for 0.1.0:

1. **Merge `action-algebra` to main** (note: `state.db` reset required — the actions table gained a `body_json` column; pre-beta "no migration shims" policy). The refactor replaced the hardcoded action-dispatch ladder (`if (name == "fadeOut") ...`) with a six-primitive algebra (`Set` / `Interpolate` / `Delay` / `Parallel` / `Sequence` / `Invoke`) + `Lua` escape-hatch op + a tiny tree interpreter. Built-ins now execute via one of three paths: static tree body with placeholder substitution, dynamic expander (morph family), or Op::Lua (setActiveTrack, trackVolume). 158 tests pass. Step 9 of the doc (bounce + virtual clock) deferred to 0.2.x — bounce still works exactly as before with frozen automation; the algebra just makes unfreezing trivial when someone gets to it.
2. **perfuce.com rebuild** (several days). Includes the tester onboarding copy carry-over — example prompts, "chat is free for testers" line, no key-paste instructions. Mostly gated on video capture.
3. **Distribution proof** (second-machine install, ~1 hour). Penultimate step before ship.
4. **Tag + release.**

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

Carry-over from pre-beta:

- [x] Code signing + notarization pipeline
- [x] Beta expiry check
- [x] Universal binary (arm64 + x86_64)
- [x] Build info in toolbar
- [x] Themed context menus via LookAndFeel
- [x] Startup song chooser (themed modal card)
- [x] Default song template (DLS Electric Piano + Audio In)
- [x] Plugin scan overlay ("Scanning plugins..." shown while AU scan runs)
- [x] Debounced autosave (3-second quiet period after last state change)
- [x] File → Open Song submenu
- [x] First-run audio device auto-selection — persists macOS default on empty config; defensive fallback via `getDefaultDeviceIndex` for aggregate/mic-denied edge cases
- [x] **Persistence integrity** (2026-04-18) — data-loss regression fixed: first-session recordings now actually persist through quit/relaunch. Saves are error-checked and roll back cleanly on any constraint violation; WAL-mode backup works; `createDefaultSong` no longer poisons `tracks.preset_id` with a plugin name; `onTrackCreated` applies `inputMonitoring` on load; `loadSong` hydrates audio region WAV files + waveforms. See `DEV_HISTORY.md` for the full list of fixes. Tests: 143/143. *Ship blocker resolved.*
- [ ] **perfuce.com rebuilt as the single docs/reference surface.** Three featured sections (AI / sequencer / perform), each with a looping UI demo video. New `/docs` route with step-by-step guides per pane (Producer / Chat / Mixer / Perform / Settings). See `../performance-testing/rounds/01-v0.1.0-first-friends/02-materials/perfuce-site-plan.md` for the checklist. No in-repo getting-started doc — the site replaces it.
- [x] "Show Log File" menu item (View → Reveal Log in Finder) + Export Logs button on the LogPane (bundles all rescued `.prev` logs + current session into a Save As dialog) as a manual-fallback if telemetry shipping ever fails.
- [x] Feedback channel — resolved: individual outreach per tester (text / email / iMessage). Documented in `performance-testing/.../welcome.md`.

### 3. Built-in AI for testers

Must-have for 0.1.0. Goal: a tester who has never touched Claude opens the Chat pane and gets meaningful help — "add a reverb to track 2," "why isn't my MIDI working." Full design + implementation history in `docs/LAMBDA_CHAT_PROXY.md`.

**Decided + shipped:**
- **API-key provisioning: Lambda proxy only.** The app POSTs chat requests to our chat-proxy Lambda which adds the Anthropic key and forwards. Per-install monthly token cap (100k input / 25k output) tracked in DynamoDB; 402 surfaced to the chat UI when exhausted. No key on tester machines. The dev-only `getenv("ANTHROPIC_API_KEY")` path is gone. "Bring your own key" Settings field deferred to 0.2.x.
- **Tool-call visibility: hidden.** `ChatView::onToolUse` is a no-op — no raw Lua, no raw errors in chat. Users see only assistant bubbles. Tool activity still goes to `/tmp/performance.log` for debugging.
- **Safety + dB API.** All gain setters clamp to [0.0, 2.0] (matches fader floor = true silence, fader top = +6dB). dB-native Lua bindings (`setTrackGainDb`, `setBusGainDb`, `addSendDb`, `setSendGainDb`) so Claude never does dB↔linear math. Prompt codifies safe defaults for new tracks / busses / sends (audio-input tracks have no input + monitoring off; new busses silent; new sends -12dB).
- **Streaming: yes.** SSE end-to-end. Lambda forwards Anthropic's stream, parses `message_delta.usage` for token counting; C++ client (`ClaudeClient::streamResponse`) consumes events incrementally and accumulates content blocks before invoking the listener.
- **Default model: server-controlled.** `DEFAULT_MODEL` env var on the chat Lambda (currently `claude-sonnet-4-5`). Bump server-side without an app rebuild. App-level `model` field removed from request body.
- **Bearer token storage.** Migrated from a CDK-managed SSM Parameter (which silently rotated on every redeploy) to AWS Secrets Manager with native auto-generation. Stable across redeploys.

**To do:**

- [x] **Refresh `runtime/CLAUDE.md`** — embedded system prompt rewritten, bundled as BinaryData so it ships with the binary, clarifies safe defaults + dB API. Continues to be tuned through self-test.
- [x] **Silent-failure surface** — resolver helpers and plugin lookups now throw with actionable errors. Claude self-corrects instead of claiming success on no-ops.
- [x] **ChatView typing indicator** — three pulsing dots while Claude is working; appears instantly on send.
- [x] **Lambda proxy** — `infra/lambda/chat.ts`; deployed as `ChatProxy` Lambda + function URL with response streaming.
- [x] **Cost guardrails** — per-install monthly token cap in Lambda; 402 + friendly message; CloudWatch billing alarm at $50/mo.
- [x] **Streaming end-to-end.** SSE wire path top to bottom.
- [ ] **Tool-use surface** — full audit of what Claude can call through `perf`. Safe-default rules cover the biggest risks; destructive-op gating (confirm / dry-run) still open.
- [ ] **ChatView UX tail** — error states polish, history persistence, clear chat, cancel in-flight, auto-focus input on pane reveal.
- [ ] **Self-test round** — Will plays for a session as a new user. Iterate prompts + tool descriptions until common asks work first-try. Ongoing through local use.
- [ ] **Tester onboarding copy** — 3–4 example prompts shown on perfuce.com (not a repo doc). Carry-over to §1 above.

### 4. Nice-to-haves considered

- [x] **Bounce to stereo file** — shipped for 0.1.0. File → Bounce… with native Save As dialog; uses the cycle region when active, else bounces 0 to the last region's endBeat. Faster-than-realtime render via `OfflineRenderer` (engine paused during bounce, graph driven from a render thread). DLS renders ~76× realtime; verified with heavy plugins. Lua surface: `bounce(path)` (cycle, throws on error) and `bounce(path, startBeat, endBeat)` (explicit). Explicit punts carried forward:
  - Automation values freeze during render (AutomationEngine pauses rather than ticking per-buffer).
  - Constant tempo (start-of-render tempo applied throughout). Proper variable tempo needs TempoMap runtime evaluation — a prerequisite, tracked separately in Backlog.
  - Constant time signature (same reason; TimeSignatureMap prerequisite).
  - Hard cutoff at end beat (no tail-time option for reverb/delay decays).
  - Master output only (no stem / per-track bouncing).
  - Plugin compatibility varies — some AUs glitch when driven faster than realtime despite `setNonRealtime(true)`. Known risk; document per-plugin as testers hit it.

  Graduating the feature to production-ready requires all the above: automation by render position, TempoMap + TimeSignatureMap honored, tail-time option, stem bouncing.
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

Tokens live in `src/gui/Theme.h` (authoritative values). Full reference — all color tables, typography, spacing, dimensions, design principles — is in `docs/THEME.md`. Read that before touching tokens or adding new ones.

#### Rules

1. **Never** use `juce::Colour(0x...)`, `juce::Colours::xxx`, or raw hex literals in GUI code. All colors come from `Theme::color(Theme::Color::xxx)`.
2. **Never** use raw font sizes (`juce::Font(14.0f)`, `Theme::font(22.0f)`). Use named tokens: `Theme::fontSizeLg`, `Theme::fontSizeSm`, etc.
3. **Never** use magic padding/spacing numbers where a token fits. Use `Theme::spacingXs/S/M/L/Xl`, `Theme::headerHeight`, `Theme::pillSize`, etc. (Layout math tied to local geometry — centering icons inside their own bounds, arc radii inside a knob — is fine; those aren't themeable.)
4. **`.withAlpha(x)` is allowed** on a theme token (e.g. `Theme::color(Theme::Color::accent).withAlpha(0.5f)`). Prefer this over a raw semi-transparent hex. If a semi-transparent color has a clear semantic role (drag-dim overlay, playhead line), add a dedicated token in Theme.h.
5. **Semantic naming over value reuse.** Several tokens share the same hex value (`bgSlot`, `bgControl`, `bgSelection` are all `0x2a2a2a`) — they're kept distinct so a future theme can vary them independently without grep-and-replace.
6. **When adding a new token**: add it to `Theme.h` in the appropriate category with a comment describing its use. Grep for similar call sites first — you may be duplicating an existing token. Also update `docs/THEME.md` and the factory JSON themes in `runtime/themes/`.

#### Token categories (see `docs/THEME.md` for values)

- **Surfaces** — `bgApp`/`bgPanel`, `bgSurface`, `bgSurfaceRaised`, `bgRecessed`
- **Interactive controls** — `bgControl`, `bgControlHover`, `bgSelection` (content panes), `bgListActive` (list views), `bgOverlay`, `bgDisabled`
- **Passive inset** — `bgSlot`, `overlayDim`
- **Text** — `textPrimary`, `textSecondary`, `textDim`, `textKeyHint`, `textOnColor`, `controlHandle`
- **Borders** — `border`, `borderSubtle`
- **Accent** — `accent`, `accentDim`
- **Transport** — `transportPlay`/`Rec`/`RecDot`/`Cycle`/`CycleOff`, `playhead`
- **Meter / activity** — `meterGreen`/`Amber`/`Red`, `sendPeak`, `activityOn`/`Off`, `statusError`
- **Track pills** — `pillMute`/`Solo`/`Arm`/`Input`/`TextOff`
- **Channel type accents** — `typeInstrument`/`typeAudio`/`typeBus`/`typeOutput` (mixer top stripe / ProducePane left stripe)

#### Design principles (brief)

- Minimal color, maximum contrast hierarchy. Color is reserved for meaning (transport, meter, status, type identity).
- Hover is an interaction signal, not decoration — only interactive controls, only when resting.
- Semantic tokens over value reuse (duplicated hex across tokens is a feature).

Full articulation of principles in `docs/THEME.md`.

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

**`InstallId`** (`src/telemetry/InstallId.h`/.cpp) — UUID generated on first launch and persisted at `~/Library/Application Support/com.performance.app/install.json` (survives state resets and version upgrades; new ID only on new machine / new account / manual wipe). Surfaced in Settings > About with a copy button.

**`TelemetryShipper`** (`src/telemetry/TelemetryShipper.h`/.cpp) — on startup, enumerates `/tmp/performance.log.*.prev` and POSTs each to the ingest endpoint on a background thread via `juce::Thread::launch` (fire-and-forget, non-blocking). On success deletes the file; on failure leaves it for the next startup to retry — so crashes ship naturally. Respects the "Send Diagnostics" toggle in Settings > About (persisted in `state.db` under `telemetry_enabled`, default on).

**Build-time configuration** — `cmake/GenerateBuildVersion.cmake` writes `build/generated/BuildVersion.h` with git commit/tag/dirty state on every build (idempotent — only rewrites the file if content changed). `cmake/GenerateBuildConfig.cmake` writes `build/generated/BuildConfig.h` with `TELEMETRY_URL` + `TELEMETRY_TOKEN` read from `keys/telemetry.json` (gitignored). Populate the keys file via `scripts/fetch-telemetry-config.sh` (pulls from AWS CloudFormation outputs + SSM). Missing keys file = empty values = shipper is a silent no-op, so fresh checkouts still build.

**AWS infra** (`infra/`) — CDK v2 TypeScript stack `PerformanceTelemetry`:
- S3 bucket `performance-session-logs-<account>` for gzipped logs (1-year lifecycle, `RETAIN` on stack destroy).
- DynamoDB `performance-installations` (pay-per-request) tracking firstSeen, lastSeen, lastCommit, lastVersion, totalBytes, totalLogs per install.
- Node.js 20 Lambda with function URL; validates `Authorization: Bearer <token>`, writes to S3 at `<installId>/<iso>-<commit>.log.gz`, upserts DDB row.
- Bearer token generated on first deploy and stored at SSM `/performance/telemetry/bearer-token`. Rotate by deleting the param and redeploying.
- $5/month budget alarm with email notification at 80%.
- Deploy: `cd infra && npx cdk deploy`. See `infra/README.md` for one-time bootstrap + rotation steps.

### IPC

`bin/perf` shell command sends Lua to the running app via `/tmp/performance.sock`. `runtime/CLAUDE.md` is the prompt for the embedded Claude instance.

## Tests

Single file: `tests/PerformanceTests.cpp`, JUCE `UnitTest` framework, ~134 tests, all isolated. `MockAudioEngine` for EngineSync verification. Coverage gaps and per-class breakdown in `DEV_HISTORY.md`.
