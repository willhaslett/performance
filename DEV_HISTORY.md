# Dev History

Changelog, test inventory, completed work, and known issues. Archived from CLAUDE.md to keep the orientation doc lean. Authoritative source for "what changed" is still `git log`; this file captures context that doesn't fit in commit messages.

## Test Suite

Single file: `tests/PerformanceTests.cpp`. JUCE `UnitTest` framework. All tests isolated (fresh state per case, temp DB files cleaned up). `MockAudioEngine` captures call log for EngineSync verification.

134 tests across 11 classes:
- **StateAPI tests (51)**: Unit — in-memory state mutations/queries. Songs, tracks, busses, effects, sends, bindings, devices, config, events, selection, score steps. Comprehensive.
- **Persistence tests (11)**: Integration — save/load round-trips. Full state, multi-song, empty DB, processor state, score steps, global bindings, custom actions, device control groups, audio config.
- **Integration tests (10)**: Coordinator→state→EngineSync→engine path. Track/bus CRUD, gains, effects, song switching, persistence through coordinator.
- **EngineSync tests (11)**: Unit — state events → MockAudioEngine calls. Song load, track rename, effect CRUD, instrument changes, audioEnabled, song switching, binding changes.
- **Audio device config tests (4)**: Integration — device name persistence, config round-trip, audioEnabled persistence and sync.
- **Sequencer tests (10)**: Unit — transport play/stop, tempo clamping, beat position advance, loop wrap, time signature, beat/transport callbacks, capabilities.
- **Arrangement tests (6)**: Unit — region CRUD, MIDI event scanning, range filtering, recording lifecycle with take capture.
- **UndoHistory tests (9)**: Unit — push/undo/redo, max steps trimming, suspend/resume, clear, empty-state edge cases.
- **SongRuntime tests (11)**: Unit — MIDI dispatch exact match, wildcard any-device, wildcard any-channel, combined wildcards, note on/off velocity normalization, pitch bend normalization, remove/clear bindings, multiple handlers.
- **Arrangement extended tests (6)**: Unit — move region (same/cross track), duplicate, split with note splitting, quantize snap with per-note shift, looped region repetition.
- **Persistence extended tests (4)**: Integration — stub binding (empty actionId) round-trip, stub score step round-trip, duplicate device control rejection, region looped/quantize field round-trip.

**Coverage gaps** (components with zero tests):
- AutomationEngine — interpolations, easing, delay
- MIDIEngine — MIDI routing, learn mode, device resolution
- LuaEngine, IPCServer — scripting and IPC layers
- Error handling — no tests for invalid inputs or error conditions
- GUI — not worth unit testing custom paint code

## Production Readiness (completed)

1. Beach ball → spinner overlay — semi-transparent overlay with message during save/load/song switch.
2. Directory permission prompt — explicit AU plugin paths, no getDefaultLocationsToSearch.
3. Audio buffer size / sample rate control — Settings window (Cmd+,) with Audio tab, also in sidebar, persisted.
4. MIDI device hot-plug — 4Hz polling, auto add/remove callbacks.
5. Error boundary — JUCE crash handler with emergency save.

(Remaining items moved to **Backlog** in `CLAUDE.md`.)

## Known Issues

**Functional:**
- AUShelfFilter crashes on instantiation — plugin bug.
- AUPitch: preset state restore doesn't take effect — AU bug.
- juce_String.cpp:327 assertion on startup — non-fatal, JUCE internals.
- No error handling on failed plugin loads (user sees nothing).
- State changes sometimes not visible until restart — watch for missed rebuildConnections/restoreBindings calls.
- Transport requires active audio device: beat clock runs in `GraphWrapper.processBlock`, so play/stop/position don't advance without an audio output device linked. Fix: fallback timer-based clock. Low priority — live use always has a device.
- DocumentWindow close button renders red despite custom LookAndFeel. JUCE's internal button drawing ignores TextButton colour overrides for the built-in close/minimize/maximize buttons. Fix: fully custom title bar component or custom LookAndFeel that overrides the specific draw method. Low priority cosmetic issue.

**Embedded Claude:**
- Bindings created via Claude/Lua may use wrong track names (case mismatch). GUI is more reliable.
- Custom action creation via Claude fails despite API working via direct IPC. Needs investigation (string escaping through chat→tool→IPC pipeline).

## Milestone v0.0.1 — Sequencer + production tools complete

MIDI + audio recording/playback, region management (select/move/copy/delete/mute), take folders, persistence, transport controls, auto-scroll, waveform display, multi-track recording, per-song tempo and time signature, preset morphing, metronome, flexible pane system, CC fader mapping. Git tag `v0.0.1`.

## Recent Additions (changelog)

- **Action track** — one per song, auto-created. Beat-triggered actions on the timeline. 3D spheres with duration tails, overlap-aware beehive layout. Double-click to create, drag to reposition, right-click to edit/delete. Persisted in `action_events` table.
- **Compound morph action** — bundles multiple sub-actions (parallel or sequential). `morphChain` for preset A → dwell → preset B transitions. MorphEditor UI with growing action slots.
- **Musical typing** — on-screen keyboard (Cmd+K). Logic-style key mapping, octave/velocity controls, sustain, draggable panel.
- **Track/region selection** — multi-track (Cmd/Shift click), multi-region selection. Track selection highlights header + selects all regions.
- **Region operations** — non-destructive quantize (right-click submenu), trim (drag edges), split at playhead (Cmd+T), join selected regions.
- **Smart grid snap** — all playhead clicks, region drags, and trims snap to division boundaries via `snapBeatToGrid()`. Shift+H/L steps by measure.
- **Cycle playback** — drag ruler to set cycle region, 'c' to toggle, 'u' to set from selection. Draggable cycle edges with resize cursor. Loop wrapping in GraphWrapper with targeted note flush (per-note bitset tracking, inline flush before scan to avoid same-sample noteOff/noteOn race). Playhead jumps to cycle start on play.
- **Ruler gutter** — dedicated playhead-setting area with adaptive bar numbers and multi-resolution tick marks. Grid clicks deselect only.
- **Note flush** — targeted noteOff using per-channel bitset of active notes. Loop flush via MidiSourceNodes only (not live MIDI path, which would race with new noteOns).
- **Fader improvements** — click-to-jump, handle center reaches full +6/-60dB range, fader/meter vertical alignment.
- **Preset name display** — editor window shows correct preset name (resolved from state on open, updated on save/load).
- **Region looping** — 'l' toggles loop, ghost copies with dashed borders and dimmed note previews. Loops until next region (or explicit loopEndBeat). Ghost right-edge resizable. Right-click ghost: Trim/Convert/Unloop. Looped regions repeat during playback via rep loop in scanMidiEvents.
- **Undo/redo** — `UndoHistory` with `deque<AppState>` (max 50 snapshots). `pushUndo()` before 39 undoable StateAPI mutations. Transactions (beginTransaction/endTransaction) for fader drags. Suspended during recording (pre-recording snapshot pushed, resumed on stop). Post-restore: arrangement pointer + audio reload + tempo sync + automation cancel. Cmd+Z / Cmd+Shift+Z.
- **Menu bar** — File/Edit/Track/View/Transport. Right-aligned gray shortcut text via NSAttributedString with tab stops. All commands dispatch through KeyBindingManager.
- **Song deletion** — right-click song in sidebar (except Sandbox). Switches to Sandbox first if deleting current song.
- **Keybinding system** — `KeyBindingManager` with 36 commands across File/Edit/Transport/View/Region/Track. User overrides stored in config, loaded on startup. `KeyBindingEditor` modal: categorized outline, click-to-capture, conflict detection, restore defaults. `handleGlobalKey` dispatches through manager — all shortcuts work globally (except when text editor focused). Pane toggle shortcuts: ⌘Y Produce, ⌘U Mappings, ⌘I Chat, ⌘⇧L Logs, ⌘O Mixer, ⌘P Sidebar.
- **Sidebar tabs** — Songs/Library/Actions/Devices. Tab-based navigation replaces flat tree. Active tab + content share lighter background. RegistryTree transparent. MIDI device entries are informational only (no selection).
- **DB backup** on every save (state.bak.db). Schema version tracking.
- **Performance Map rewrite** — two-pane Controllers + Song Mappings layout. Collapsible device tree, drag-and-drop between all areas (controllers → atemporal/score, atemporal → score, score → atemporal, score reorder with insertion line). Learn mode with port-aware device resolution, duplicate flash, IAC filtering. Inline name editing with up/down arrow navigation, group field with smart picker menu. Stub bindings (no action) persist. Score grows vertically, atemporal scrolls. `InlineEditor` re-entrancy guard (`isCommitting`) prevents double-commit on focusLost.
- **MIDI device auto-registration** — `PerformanceCoordinator::refreshMidiDevices` registers connected devices on startup and hot-plug. No manual registration step. IAC Driver buses filtered.
- **Input monitoring** — `inputMonitoring` flag on audio input tracks. Disconnects live audio input from track signal chain when off. "I" pill button in arrange view, toggle in mixer.
- **Solo and Mute** — runtime flags on TrackState. Mute via `GainProcessor.setMuted()` (preserves user gain). Solo via `AudioEngine::updateMuteStates()` (mutes all non-soloed when any soloed). M/S pill buttons: mixer bottom row, arrange view row 2.
- **Theme redesign** — semantic color tokens, unified surface hierarchy (bgApp → bgSurface → bgSlot), neutral track headers (no per-track color), two-row track headers with pill buttons, subdued pill colors, spacing/typography scales. All hardcoded colors migrated to Theme.h tokens.
- **Fail-loud assertions** — `PERF_ASSERT` macro, asserting helpers `song()`/`track()`/`bus()`/`device()` in StateAPI, assertions in EngineSync and Arrangement. Silent failures replaced with crashes for programming errors.
- **Audio region persistence** — takes table now stores file_path, record_tempo, sample_rate, channel_count.

## LOC

~25,400 lines of source code (headers + implementation + tests). See `find src tests -name "*.h" -o -name "*.cpp" -o -name "*.mm" | xargs wc -l`.
